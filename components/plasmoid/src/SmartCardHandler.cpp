// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "SmartCardHandler.h"

#include "Cards.h" // LibreKDE::Cards::hasCapability
#include "DisplayText.h"
#include "ErrorText.h"
#include "IdentityRows.h" // LibreKDE::localizedFieldLabel
#include "Readers.h"
#include "SignJob.h"
#include "plasmoid_log_categories.h"

#include <LibreSCRS/AgentClient/AgentCapabilities.h>
#include <LibreSCRS/AgentClient/AgentCard.h>
#include <LibreSCRS/AgentClient/AgentClient.h>
#include <LibreSCRS/AgentClient/AgentOperation.h>
#include <LibreSCRS/AgentClient/AgentReader.h>
#include <LibreSCRS/AgentClient/IdentityRows.h>
#include <LibreSCRS/AgentClient/SealedPayload.h>
#include <LibreSCRS/AgentClient/SharedAgentClient.h>
#include <LibreSCRS/AgentClient/SignOptions.h> // PhotoItem, the photo result's element type

#include <KIO/CommandLauncherJob>
#include <KLocalizedString>
#include <KService>
#include <KShell>

#include <QBuffer>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QLatin1StringView>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace LibreKDE::Plasmoid {

// Short local spelling for the agent client library, whose value types this
// handler consumes directly — the flattened identity row among them. The host
// adds only what that library will not: the label table keyed on the frozen
// label key (IdentityRows) and the localized failure copy (ErrorText).
namespace Client = LibreSCRS::AgentClient;

namespace {

// The one finder the plasmoid needs beyond the shared Readers helpers: a pure
// function of the client's deterministic `readers()` view, name-keyed for the
// master-detail chips, so only this component carries it.

/// The first reader of the given friendly @p name that holds a resolvable card.
/// Two identically-named readers collide on the first match.
Client::AgentReader* readerWithCardByName(Client::AgentClient& client, const QString& name)
{
    for (Client::AgentReader* reader : client.readers()) {
        if (reader != nullptr && reader->name() == name && reader->card() != nullptr) {
            return reader;
        }
    }
    return nullptr;
}

} // namespace

quint64 SmartCardHandler::nextPhotoSlot()
{
    // Process-unique, monotonic: two widgets never write the same store slot.
    static std::atomic<quint64> counter{0};
    return ++counter;
}

SmartCardHandler::SmartCardHandler(QObject* parent) : SmartCardHandler(Client::sharedAgentClient(), parent) {}

SmartCardHandler::SmartCardHandler(std::shared_ptr<Client::AgentClient> client, QObject* parent)
    : QObject(parent), m_client(std::move(client))
{
    qCInfo(LibreKDE::Plasmoid::Logging)
        << "SmartCardHandler starting (per-widget instance over the shared agent client)";
    wireClient();
    refresh();
}

SmartCardHandler::~SmartCardHandler()
{
    // Scrub this widget's photo slot from the process-shared store; the store
    // (and the provider co-owning it) outlives any single handler.
    m_photoStore->clear(m_photoSlot);
}

int SmartCardHandler::state() const noexcept
{
    return static_cast<int>(m_state);
}

void SmartCardHandler::wireClient()
{
    connect(m_client.get(), &Client::AgentClient::readersChanged, this, &SmartCardHandler::refresh);
    connect(m_client.get(), &Client::AgentClient::cardChanged, this, [this](const QString&) { refresh(); });
    connect(m_client.get(), &Client::AgentClient::availabilityChanged, this, [this](bool) { refresh(); });
    connect(m_client.get(), &Client::AgentClient::agentStartFinished, this, &SmartCardHandler::onAgentStartFinished);
}

void SmartCardHandler::refresh()
{
    // Availability first: an unreachable agent is a client-level state, not a
    // NoCard. Never hang — isAvailable() is a cached, non-blocking flag. The
    // roster recompute still runs (the registry was cleared on service loss),
    // so the QML master-detail never lingers around the AgentUnavailable state.
    if (!m_client->isAvailable()) {
        updateReaderRosters();
        bindCard(nullptr);
        updatePinManagementAvailable();
        setReaderName({});
        setCardLabel({});
        setError({});
        setCardDetected(false);
        m_identityRead = false;
        setAgentInstalled(m_client->agentState() != Client::AgentState::Missing);
        transitionTo(CardStateModel::State::AgentUnavailable);
        publishSignSurfaces();
        return;
    }

    // Recompute the reader rosters (config chooser + Auto master list) from the
    // live registry, then resolve which card this widget reflects. Selection is
    // centralized + deterministic (sorted-path order) in AgentClient.
    updateReaderRosters();

    Client::AgentReader* reader = pickActiveReader();
    if (reader == nullptr) {
        bindCard(nullptr);
        updatePinManagementAvailable();
        // Bound mode: keep the bound name visible so the QML can render a
        // reader-scoped NoCard state — "Insert a card into <name>" when the
        // reader is present (boundReaderPresent), or "Reader <name> not
        // connected" when it is absent. Auto: clear it.
        setReaderName(m_boundReaderName);
        setCardLabel({});
        setError({});
        // A reader may physically report a card here even though no resolvable
        // Card1 exists yet (the agent's deferred-publish window): flag it so the
        // QML says "detecting a card" rather than "insert a card".
        setCardDetected(computeCardDetected());
        m_identityRead = false;
        transitionTo(CardStateModel::State::NoCard);
        publishSignSurfaces();
        return;
    }

    setCardDetected(false);
    setReaderName(reader->name());
    bindCard(reader->card());
    classifyActiveCard();
    // The displayed reader is an INPUT to the sign surfaces, so a re-target
    // moves them even though no sign started or finished.
    publishSignSurfaces();
}

void SmartCardHandler::updateReaderRosters()
{
    QStringList all;
    QStringList withCards;
    // Raw name -> label, keyed by the raw name the rest of the code binds and
    // selects on. The agent labels every reader against the whole roster and
    // relabels a reader when another arrives or leaves beside it, which reaches
    // here as a change of that reader -- so the map is rebuilt every time.
    QHash<QString, QString> labels;
    const QList<Client::AgentReader*> readers = m_client->readers();
    for (Client::AgentReader* reader : readers) {
        all.append(reader->name());
        labels.insert(reader->name(), Readers::readerLabel(*reader));
        if (reader->card() != nullptr) {
            withCards.append(reader->name());
        }
    }

    // A relabel leaves the names as they were, but a view listing them shows
    // the labels, so it is told the list changed either way.
    if (all != m_availableReaderNames || labels != m_readerDisplayNames) {
        m_availableReaderNames = all;
        m_readerDisplayNames = std::move(labels);
        Q_EMIT availableReaderNamesChanged();
    }
    if (withCards != m_readersWithCards) {
        m_readersWithCards = withCards;
        Q_EMIT readersWithCardsChanged();
    }
    // Forget departed readers, but never one still signing: a job outlives a
    // re-target by design and has to survive to report its outcome.
    for (auto it = m_signStates.begin(); it != m_signStates.end();) {
        if (it->job == nullptr && !all.contains(it.key())) {
            it = m_signStates.erase(it);
        } else {
            ++it;
        }
    }

    // Recompute after the roster is settled, unconditionally: boundReaderPresent
    // can flip on a binding change even when the roster itself did not move.
    updateBoundReaderPresent();
}

void SmartCardHandler::updateBoundReaderPresent()
{
    // Present iff a reader is bound AND it is currently in the live roster
    // (`availableReaderNames` lists every reader the agent reports, card or not).
    // Exact name first; the same-unit fallback applies ONLY when the bound name
    // has vanished from the roster (re-enumeration) — while it is still listed,
    // a dual-interface sibling sharing the unit serial must not stand in for it.
    // Auto mode (no binding) is never "present". Fire only on an actual flip.
    bool present = false;
    if (!m_boundReaderName.isEmpty()) {
        present = m_availableReaderNames.contains(m_boundReaderName);
        if (!present) {
            for (const QString& name : m_availableReaderNames) {
                if (sameReaderUnit(m_boundReaderName, name)) {
                    present = true;
                    break;
                }
            }
        }
    }
    if (present == m_boundReaderPresent) {
        return;
    }
    m_boundReaderPresent = present;
    Q_EMIT boundReaderPresentChanged();
}

Client::AgentReader* SmartCardHandler::pickActiveReader()
{
    if (!m_boundReaderName.isEmpty()) {
        // Bound mode: ONLY the bound reader, and only when it holds a resolvable
        // card. Exact name first — stable rosters never change behaviour.
        if (Client::AgentReader* exact = readerWithCardByName(*m_client, m_boundReaderName)) {
            return exact;
        }
        const QList<Client::AgentReader*> readers = m_client->readers();
        // The same-unit fallback exists for RE-ENUMERATION (shifted trailing
        // index re-mints the entry under a new name), so it engages ONLY when
        // the bound name has vanished from the roster. While the bound reader
        // is still listed — merely card-less — never fall back: on a
        // dual-interface reader (contact + contactless expose one shared USB
        // serial as two simultaneous entries) a card laid on the sibling
        // interface must not capture this widget. Reader-scoped waiting is the
        // honest state.
        for (Client::AgentReader* reader : readers) {
            if (reader->name() == m_boundReaderName) {
                return nullptr;
            }
        }
        for (Client::AgentReader* reader : readers) {
            if (sameReaderUnit(m_boundReaderName, reader->name()) && reader->card() != nullptr) {
                return reader;
            }
        }
        return nullptr;
    }
    // Auto mode: honour a transient master-detail pick while it still holds a
    // card; otherwise the deterministic first (mirrors firstReaderWithCard).
    if (!m_selectedReaderName.isEmpty()) {
        if (Client::AgentReader* selected = readerWithCardByName(*m_client, m_selectedReaderName)) {
            return selected;
        }
    }
    return Readers::firstReaderWithCard(*m_client);
}

bool SmartCardHandler::computeCardDetected() const
{
    // Reached only from the card-less refresh branch, so pickActiveReader()
    // found no reader holding a RESOLVABLE card. A reader that nonetheless
    // reports HasCard() therefore has a card the agent has not yet exported a
    // Card1 for (the deferred-publish window) — genuinely "a card is here, being
    // read", not "no card".
    if (!m_boundReaderName.isEmpty()) {
        // Bound mode: answer for the SAME entry pickActiveReader() would
        // choose. While the bound name is listed, only THAT reader's slot
        // counts — a dual-interface sibling's card must not flip the bound
        // widget's "insert a card" into "detecting".
        const QList<Client::AgentReader*> readers = m_client->readers();
        for (Client::AgentReader* reader : readers) {
            if (reader->name() == m_boundReaderName) {
                return reader->hasCard();
            }
        }
        // Bound name absent (re-enumerated): any same-unit candidate that
        // physically reports a card is the entry pickActiveReader() will pick
        // once its Card1 resolves — scan them ALL (a transient double
        // enumeration can leave an empty stale twin sorting first).
        for (Client::AgentReader* reader : readers) {
            if (sameReaderUnit(m_boundReaderName, reader->name()) && reader->hasCard()) {
                return true;
            }
        }
        return false;
    }
    // Auto mode: any reader physically holding a card.
    for (Client::AgentReader* reader : m_client->readers()) {
        if (reader->hasCard()) {
            return true;
        }
    }
    return false;
}

void SmartCardHandler::setCardDetected(bool detected)
{
    if (m_cardDetected == detected) {
        return;
    }
    m_cardDetected = detected;
    Q_EMIT cardDetectedChanged();
}

void SmartCardHandler::setBoundReaderName(const QString& name)
{
    if (m_boundReaderName == name) {
        return;
    }
    m_boundReaderName = name;
    // A rebind invalidates any Auto-mode transient pick.
    m_selectedReaderName.clear();
    Q_EMIT boundReaderNameChanged();
    refresh();
}

void SmartCardHandler::selectReader(const QString& friendlyName)
{
    // Master-detail is an Auto-mode affordance; a bound widget is pinned.
    if (!m_boundReaderName.isEmpty()) {
        return;
    }
    if (m_selectedReaderName == friendlyName) {
        return;
    }
    m_selectedReaderName = friendlyName;
    refresh();
}

void SmartCardHandler::bindCard(Client::AgentCard* card)
{
    // Short-circuit only when re-binding the SAME, still-live card (a genuine
    // no-op). When `card == nullptr` we must NOT short-circuit on `m_card == card`:
    // a removed card destroys the AgentCard, so the `m_card` QPointer has already
    // auto-nulled to nullptr too — `nullptr == nullptr` would skip the photo/
    // identity scrub and leave the holder PII in the store after removal.
    if (card != nullptr && m_card == card) {
        return;
    }
    if (m_card != nullptr) {
        m_card->disconnect(this);
    }
    if (m_identityOp != nullptr) {
        // Mirror the credentials window's re-target rule: stop listening BEFORE
        // the agent-side cancel, then reap. The fire-and-forget Cancel dismisses
        // any secure prompt the abandoned read may have raised (a re-target must
        // never leave an orphaned CAN prompt behind). Reached with a LIVE op on
        // a card SWITCH mid-read (chip click / rebind — no client death-sweep
        // terminalizes the op then).
        m_identityOp->disconnect(this);
        m_identityOp->cancel();
        m_identityOp->deleteLater(); // parented to the old card; reap our read early
        m_identityOp = nullptr;
        // The discarded op can no longer drive onOperationFinished (the only
        // other setBusy(false) site), so release the busy latch HERE; left
        // latched, busy permanently disables the PreAuth "Read Card…" button.
        // The photo op below needs no counterpart — it carries no busy flag.
        setBusy(false);
    }
    if (m_photoOp != nullptr) {
        m_photoOp->disconnect(this);
        m_photoOp->cancel();
        m_photoOp->deleteLater();
        m_photoOp = nullptr;
    }
    m_card = card;
    m_identityRead = false;
    // A new (or no) card invalidates any photo AND identity read for the previous one.
    clearPhoto();
    clearIdentity();
    if (m_card != nullptr) {
        // A live capability change (e.g. post-PACE the agent surfaces more
        // caps) re-classifies the active card.
        connect(m_card, &Client::AgentCard::changed, this, &SmartCardHandler::classifyActiveCard);
    }
}

void SmartCardHandler::setViewActive(bool active)
{
    if (m_viewActive == active) {
        return;
    }
    m_viewActive = active;
    Q_EMIT viewActiveChanged();
    if (m_viewActive) {
        // The popup just opened: first view of whatever card is active.
        ensureFreeRead();
    }
}

void SmartCardHandler::classifyActiveCard()
{
    // Every (re)classification refreshes the launcher gate from the LIVE caps —
    // including the AgentCard::changed path, where the coarse state may not move.
    updatePinManagementAvailable();

    if (m_card == nullptr) {
        transitionTo(CardStateModel::State::NoCard);
        return;
    }

    // The capability set arrives as stable TOKENS; the resolvers take the
    // bitfield. capabilityBits() is the exact inverse of the tokenizer — a bit
    // this build has no name for round-trips as "bit<i>" rather than being
    // dropped — so the round-trip is lossless.
    //
    // resolveCardState owns the rest: the empty-capability None -> Error /
    // caps==0 -> UnknownCard split, and the pre-auth latch. The latch's input is
    // the DECODED unlock method, a vocabulary with no way to say "an unlock is
    // required, method unknown", so the requirement comes from the client
    // library's own rule (preReadUnlockRequired: every method, known or not,
    // except None) and only the same latch rule — it holds until an identity
    // read has succeeded — is applied to it. With
    // nothing left to unlock, the resolver is handed None, which is then the
    // truthful input rather than a synthesized method.
    // fromUiState maps the result onto the plasmoid's State. (When
    // PreAuthRequired we also clear the label, matching the previous behaviour.)
    const bool unlockPending = Client::preReadUnlockRequired(m_card->preReadAuth()) && !m_identityRead;
    const Client::UiState ui =
        unlockPending ? Client::UiState::PreAuthRequired
                      : Client::resolveCardState(Client::capabilityBits(m_card->capabilities()),
                                                 Client::PreReadAuth::None, /*present=*/true, m_identityRead);
    if (ui == Client::UiState::PreAuthRequired) {
        setCardLabel({});
    }
    transitionTo(CardStateModel::fromUiState(ui));

    // The free read is issued here ONLY while the view is active:
    // classification runs at insertion for every widget in the process, and
    // the free-read rule only allows auto-populating "on first view" — an unconditional read
    // would park identity+photo PII in plasmashell memory even for widgets
    // whose popup is never opened. Hanging the check off the classification
    // (rather than off stateChanged edges in QML) covers every ACTIVE-CARD
    // change too: a chip switch between two same-classification cards or a
    // same-reader card swap fires no stateChanged, yet must re-populate the
    // open popup. ensureFreeRead() itself keeps the invariants: never
    // a Can/Mrz card, no-op when already read or in flight.
    if (m_viewActive) {
        ensureFreeRead();
    }
}

void SmartCardHandler::ensureFreeRead()
{
    if (m_card == nullptr) {
        return;
    }
    // Only a card that announces NO pre-read unlock and carries identity data
    // has a secret-free read — the "free read" allowed on first view. A card
    // that announces one is deliberately EXCLUDED, whether or not this build
    // can name the method: that keeps the lazy invariant (the agent would raise
    // an unlock prompt, which must be user-initiated;
    // CanCardIssuesNoImplicitCardIo asserts this). The !m_identityRead guard
    // makes repeat popup-opens no-ops; readIdentity() itself guards the
    // in-flight case.
    if (Client::preReadUnlockRequired(m_card->preReadAuth()) || m_identityRead || m_identityOp != nullptr) {
        return;
    }
    if (m_state != CardStateModel::State::IdentityOnly && m_state != CardStateModel::State::Hybrid) {
        return;
    }
    readIdentity();
}

void SmartCardHandler::readIdentity()
{
    if (m_card == nullptr) {
        return;
    }
    if (m_identityOp != nullptr) {
        return; // a read is already in flight
    }
    setError({});
    // Non-null on every path, including a refusal: a call the agent rejects at
    // entry comes back as an operation that is ALREADY finished, carrying the
    // mapped failure, so there is no null to test for and the refusal reaches
    // the user through the same terminal handling every other outcome does.
    Client::AgentOperation* op = m_card->readIdentity();
    op->setStallBudget(m_opStallBudgetMs);
    m_identityOp = op;
    setBusy(true);
    setOperationPhase(Client::OperationPhase::Created); // reset for the new read
    connect(op, &Client::AgentOperation::finished, this, &SmartCardHandler::onOperationFinished);
    connect(op, &Client::AgentOperation::phaseChanged, this,
            [this](Client::OperationPhase ph, double /*progress*/) { setOperationPhase(ph); });
}

void SmartCardHandler::warmCertificateCache()
{
    if (m_card == nullptr) {
        return;
    }
    // A warm is an OPTIMIZATION, and an optimization must never cost a
    // credential prompt: on a pre-read-gated card (CAN) the agent-side cert
    // read would raise the prompt this handler deliberately never triggers on
    // its own — the same policy the identity path applies. The PKI folder
    // pays its own cold read there, WITH the prompt, at the moment the user
    // actually opens it.
    if (Client::preReadUnlockRequired(m_card->preReadAuth())) {
        return;
    }
    // Best-effort background warm: deliberately NO setBusy / NO state transition /
    // NO error surfacing — the identity view is unaffected and a failed warm just
    // leaves the file-manager PKI folder to pay its own cold read. The entry call
    // is asynchronous (AgentCard::warmCertificates), so even a wedged agent can
    // never stall the GUI thread here, and it mints nothing to hold: a warm
    // issued while one is still in flight for this card is a no-op inside the
    // client, so a later open may warm again freely — the agent dedups
    // overlapping cert reads onto one shared card read.
    m_card->warmCertificates();
}

const SmartCardHandler::ReaderSignState* SmartCardHandler::activeSignState() const
{
    const auto it = m_signStates.constFind(m_readerName);
    if (it == m_signStates.constEnd()) {
        return nullptr;
    }
    // An entry speaks only for the card it was started on. A card swapped into
    // the same reader is a different AgentCard, so it inherits nothing — and a
    // removed card nulls both sides, which the card-less states never render.
    return it->card == m_card ? &*it : nullptr;
}

bool SmartCardHandler::signingBusy() const
{
    const ReaderSignState* entry = activeSignState();
    return entry != nullptr && entry->job != nullptr;
}

int SmartCardHandler::signPhase() const
{
    const ReaderSignState* entry = activeSignState();
    return entry != nullptr ? entry->phase : static_cast<int>(Client::OperationPhase::Created);
}

QVariantMap SmartCardHandler::signResult() const
{
    const ReaderSignState* entry = activeSignState();
    const SignOutcome outcome = entry != nullptr ? entry->outcome : SignOutcome::None;
    QVariantMap out;
    out.insert(QStringLiteral("outcome"), static_cast<int>(outcome));
    out.insert(QStringLiteral("outputPath"), entry != nullptr ? entry->outputPath : QString());
    out.insert(QStringLiteral("certLabel"), entry != nullptr ? entry->certLabel : QString());
    out.insert(QStringLiteral("level"), entry != nullptr ? entry->level : QString());
    out.insert(QStringLiteral("message"), entry != nullptr ? entry->message : QString());
    return out;
}

void SmartCardHandler::dismissSignResult()
{
    const auto it = m_signStates.find(m_readerName);
    if (it == m_signStates.end() || it->outcome == SignOutcome::None) {
        return;
    }
    it->outcome = SignOutcome::None;
    it->outputPath.clear();
    it->certLabel.clear();
    it->level.clear();
    it->message.clear();
    publishSignSurfaces();
}

void SmartCardHandler::publishSignSurfaces()
{
    const bool busy = signingBusy();
    if (busy != m_publishedSigningBusy) {
        m_publishedSigningBusy = busy;
        Q_EMIT signingBusyChanged();
    }
    const int phase = signPhase();
    if (phase != m_publishedSignPhase) {
        m_publishedSignPhase = phase;
        Q_EMIT signPhaseChanged();
    }
    const QVariantMap result = signResult();
    if (result != m_publishedSignResult) {
        m_publishedSignResult = result;
        Q_EMIT signResultChanged();
    }
}

void SmartCardHandler::signFile(const QString& fileUrl)
{
    // Keyed by the reader, not by the widget: a second reader must be able to
    // start its own sign while this one runs (the agent raises one credential
    // prompt per operation and serialises per card, not per client).
    const QString reader = m_readerName;
    if (m_signStates.value(reader).job != nullptr) {
        return; // this reader is already signing
    }
    // The QML FileDialog hands us a file:// URL; accept a plain path too.
    const QUrl url(fileUrl);
    const QString inputPath = url.isLocalFile() ? url.toLocalFile() : fileUrl;

    setError({});
    // ONE signing CORE (the shared librekde-signing SignJob) over the active card,
    // but the plasmoid injects its OWN non-QtWidgets seams: a QML/Plasma popup must
    // never raise a parentless top-level QWidget modal (QInputDialog/QMessageBox),
    // which is un-idiomatic and asserts if plasmashell is a QGuiApplication (not a
    // QApplication). The Purpose plugin keeps the QtWidgets SignSeams; here:
    //  - cert chooser: SignJob auto-picks a lone signing cert, so this only fires
    //    when a card carries several. The plasmoid signs with the first
    //    (deterministic, matching its "obvious card" model); rich multi-cert
    //    selection is a LibreCelik concern. Because that
    //    pick is silent, the chooser records WHICH cert it picked so the
    //    success message can say so (never a silent implicit choice).
    // MIME: the plasmoid has no share-time MIME (the input comes from a FileDialog),
    // so it passes an empty mimeType and SignJob sniffs via QMimeDatabase. For a
    // well-formed file this resolves to the same {format,packaging} the Purpose
    // path derives from its caller-supplied MIME;
    // the only boundary is a file whose sniffed MIME differs from a share-time MIME,
    // which the plasmoid cannot see. A null/non-PKI card is handled by SignJob
    // itself (it fails cleanly with its own diagnostic). The agent raises its own
    // PIN prompter.
    LibreKDE::CertChooser pickFirstCert =
        [this, reader](const QList<Client::CertificateInfo>& cands) -> std::optional<QString> {
        if (cands.isEmpty()) {
            return std::nullopt;
        }
        // Only reached for a MULTI-cert card (SignJob auto-picks a lone cert):
        // remember the picked cert's display name for the success message.
        const Client::CertificateInfo& picked = cands.first();
        m_signStates[reader].certLabel = picked.subject.isEmpty() ? picked.id : picked.subject;
        return std::optional<QString>(picked.id);
    };

    // Parented to the handler, not the card: a sign the user started keeps
    // running across a chip switch (only the identity read is re-targeted).
    auto* job = new LibreKDE::SignJob(m_card, inputPath, QString(), QString(), pickFirstCert, this);
    ReaderSignState& entry = m_signStates[reader];
    entry.job = job;
    entry.card = m_card;
    entry.phase = static_cast<int>(Client::OperationPhase::Created); // reset for the new sign
    entry.certLabel.clear();
    // A new sign supersedes this reader's previous outcome, so the banner never
    // shows a stale success next to a running spinner.
    entry.outcome = SignOutcome::None;
    entry.outputPath.clear();
    entry.level.clear();
    entry.message.clear();
    publishSignSurfaces();

    connect(job, &LibreKDE::SignJob::phaseChanged, this,
            [this, reader](Client::OperationPhase ph, double /*progress*/) {
                const auto it = m_signStates.find(reader);
                if (it == m_signStates.end()) {
                    return;
                }
                it->phase = static_cast<int>(ph);
                publishSignSurfaces();
            });

    connect(job, &LibreKDE::SignJob::succeeded, this, [this, reader, job](const QString& outputPath) {
        ReaderSignState& done = m_signStates[reader];
        done.job = nullptr;
        done.outcome = SignOutcome::Succeeded;
        done.outputPath = outputPath;
        // The level the agent REPORTS having produced, not one this client
        // asked for — it does not ask.
        done.level = job->signMeta().value(QStringLiteral("level")).toString();
        done.message.clear();
        publishSignSurfaces();
        qCInfo(LibreKDE::Plasmoid::Logging) << "signed file written to" << outputPath;
        job->deleteLater();
    });
    connect(job, &LibreKDE::SignJob::failed, this, [this, reader, job](const QString& message) {
        // Deliberately NOT setError(): `errorMessage` is the widget's single
        // card-state surface, so a sign failure routed through it would surface
        // on another reader's failed-card page.
        ReaderSignState& done = m_signStates[reader];
        done.job = nullptr;
        done.outcome = SignOutcome::Failed;
        done.outputPath.clear();
        done.level.clear();
        done.message = message;
        publishSignSurfaces();
        job->deleteLater();
    });
    job->start();
}

void SmartCardHandler::onOperationFinished()
{
    Client::AgentOperation* op = m_identityOp;
    m_identityOp = nullptr;
    setBusy(false);
    if (op == nullptr) {
        return; // the op was reaped between the terminal and this slot
    }
    // Released, not left parented to the card until it is pulled: a finished
    // read still holds the holder's whole identity. deleteLater() takes effect
    // on the next event-loop turn, so the reads below run on a live object.
    op->deleteLater();
    if (op->status() == Client::OperationStatus::Cancelled && op->errorCode() == Client::ErrorCode::None &&
        op->callError() == Client::CallError::None) {
        // The holder dismissed the agent's prompt: nothing failed, so the
        // widget goes back to where the read started (the unlock button of a
        // card that needs one) rather than to an error page. A cancel the
        // client itself declares — the agent gone, the card pulled — carries a
        // code or a call error and stays an error below.
        setError({});
        classifyActiveCard();
        return;
    }
    if (op->status() != Client::OperationStatus::Ok) {
        setError(
            LibreKDE::ErrorText::forOutcome(op->errorCode(), op->callError(), op->messageKey(), op->messageFallback()));
        transitionTo(CardStateModel::State::Error);
        return;
    }
    m_identityRead = true;
    rebuildIdentityModel(op->identityResult());
    classifyActiveCard();
    // The identity is in hand; opportunistically fetch the face photo. Best
    // effort — a card with no photo (or a photo read that fails) must not turn a
    // successful identity read into an error.
    startPhotoRead();
}

void SmartCardHandler::startPhotoRead()
{
    if (m_card == nullptr) {
        return;
    }
    if (m_photoOp != nullptr) {
        return; // a photo read is already in flight
    }
    // Non-null on every path, refusal included (see readIdentity): a card
    // without a photo surface comes back already finished, and the terminal
    // handler below logs why and leaves hasCardPhoto false.
    Client::AgentOperation* op = m_card->getPhoto();
    op->setStallBudget(m_opStallBudgetMs);
    m_photoOp = op;
    connect(op, &Client::AgentOperation::phaseChanged, this,
            [this](Client::OperationPhase ph, double /*progress*/) { setOperationPhase(ph); });
    // Drive on the op's terminal signal (queued per the client's terminal
    // discipline). Async/signal-driven: NO nested QEventLoop — the plasmoid runs
    // on the GUI event loop. If the card is pulled mid-read the op is destroyed
    // and this connection auto-disconnects (QPointer also guards reentry).
    connect(op, &Client::AgentOperation::finished, this, &SmartCardHandler::onPhotoFinished);
}

void SmartCardHandler::onPhotoFinished()
{
    Client::AgentOperation* op = m_photoOp;
    m_photoOp = nullptr;
    if (op == nullptr) {
        return;
    }
    op->deleteLater(); // as in onOperationFinished: the photo is copied out below
    if (op->status() != Client::OperationStatus::Ok) {
        // Best effort: no photo shown. Say why — a refused entry lands here too.
        // Quoted deliberately: the fallback can carry card-derived text, and
        // QDebug's quoting escapes control characters — an embedded newline must
        // not be able to forge a journal line.
        qCWarning(LibreKDE::Plasmoid::Logging)
            << "photo read failed:" << static_cast<int>(op->errorCode()) << op->messageFallback();
        return;
    }
    const std::vector<Client::PhotoItem> photos = op->takePhotos();
    if (photos.empty()) {
        // Graceful absence, not an error — but distinguishable from a failure.
        qCInfo(LibreKDE::Plasmoid::Logging) << "card carries no photo";
        return;
    }
    // v1: surface the FIRST photo item. (A future layout could expose every
    // keyed photo.) Which item that is depends on the transport, so this picks
    // "a photo", not "the portrait": the D-Bus transport builds the vector by
    // walking a key-sorted map, so first means lowest key, while the socket
    // transport preserves the order the agent sent. Every card supported today
    // carries at most one, so the two agree in practice.
    const std::optional<QByteArray> payload = Client::readBoundedPayload(photos.front().fd);
    if (!payload.has_value()) {
        // A genuine read failure, kept distinct from the empty-but-valid case
        // below — the reader separates them deliberately (a disengaged optional
        // means the descriptor was unusable, refused or short-read).
        qCWarning(LibreKDE::Plasmoid::Logging) << "photo payload could not be read";
        return;
    }
    if (payload->isEmpty()) {
        // A legitimately zero-length payload: the card announced a photo field
        // and delivered no bytes. Absence, not failure.
        qCInfo(LibreKDE::Plasmoid::Logging) << "photo payload is empty — no photo shown";
        return;
    }
    const QByteArray bytes = *payload;
    // Let Qt sniff the format. eMRTD photos may be JPEG2000; Qt decodes it iff the
    // jp2 image plugin is present, otherwise QImage::fromData yields a null image,
    // which the QML treats exactly like "no photo".
    QImage image = QImage::fromData(bytes);
    if (image.isNull()) {
        qCWarning(LibreKDE::Plasmoid::Logging)
            << "photo bytes did not decode (" << bytes.size() << "bytes) — missing image plugin?";
        return;
    }
    m_photoBytes = bytes; // retained (original format) for Save photo…
    m_photoSuggestedFileName = suggestedPhotoFileName(bytes);
    m_photoStore->setImage(m_photoSlot, image);
    m_hasCardPhoto = true;
    // Bump the token so the QML pixmap cache re-requests this (new) photo; the
    // slot addresses THIS widget's store entry (per-widget isolation).
    ++m_photoToken;
    m_cardPhotoUrl = QStringLiteral("image://librekde/cardphoto/%1?%2").arg(m_photoSlot).arg(m_photoToken);
    Q_EMIT cardPhotoChanged();
}

void SmartCardHandler::clearPhoto()
{
    m_photoBytes.clear();
    if (m_photoStore != nullptr) {
        m_photoStore->clear(m_photoSlot);
    }
    if (!m_hasCardPhoto && m_cardPhotoUrl.isEmpty()) {
        return;
    }
    m_hasCardPhoto = false;
    m_cardPhotoUrl.clear();
    m_photoSuggestedFileName.clear();
    Q_EMIT cardPhotoChanged();
}

QString SmartCardHandler::suggestedPhotoFileName(const QByteArray& bytes)
{
    // Sniff the RAW bytes' actual format — savePhoto writes those bytes
    // verbatim, so the suggested extension must match the content (an eMRTD
    // photo may be JPEG2000; ".png" would lie about jp2 bytes).
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QByteArray format = QImageReader(&buffer).format();
    if (format == "jpeg") {
        format = "jpg"; // the conventional extension
    }
    const QString base = QStringLiteral("card-photo");
    return format.isEmpty() ? base : base + QLatin1Char('.') + QString::fromLatin1(format);
}

void SmartCardHandler::openInLibreCelik()
{
    // Conditional: the QML side hides this affordance when LibreCelik is absent,
    // and we never route to a browser (no librecelik:// URL scheme). Launched
    // with no arguments: LibreCelik takes none and shows every reader itself.
    if (m_libreCelikPath.isEmpty()) {
        qCWarning(LibreKDE::Plasmoid::Logging) << "openInLibreCelik ignored — LibreCelik not detected on PATH";
        return;
    }
    if (!QProcess::startDetached(m_libreCelikPath, {})) {
        qCWarning(LibreKDE::Plasmoid::Logging) << "failed to launch LibreCelik:" << m_libreCelikPath;
    }
}

// The standalone credential-management window executable (components/credentials);
// manageCredentials() launches it for the active card's reader. constexpr
// gives it internal linkage, so no anonymous namespace is needed.
constexpr char kCredentialsExe[] = "librescrs-credentials-kde";

bool SmartCardHandler::pinManagementAvailable() const
{
    return m_pinManagementAvailable;
}

void SmartCardHandler::updatePinManagementAvailable()
{
    // Purely capability-bit-driven — no card read is issued. False when no card
    // is bound, mirroring every other card-derived flag. The dedicated change
    // signal is what keeps the QML launcher affordance live: transitionTo()
    // dedupes stateChanged, so a capability flip that keeps the coarse state
    // (Hybrid stays Hybrid) fires only this.
    const bool available = m_card != nullptr && LibreKDE::Cards::hasCapability(*m_card, Client::Cap::PinManagement);
    if (m_pinManagementAvailable == available) {
        return;
    }
    m_pinManagementAvailable = available;
    Q_EMIT pinManagementAvailableChanged();
}

QStringList SmartCardHandler::credentialsLaunchArgs(const QString& readerId)
{
    return {QStringLiteral("--reader"), readerId};
}

QString SmartCardHandler::readerDisplayName(const QString& rawName) const
{
    return m_readerDisplayNames.value(rawName, rawName);
}

void SmartCardHandler::manageCredentials()
{
    if (!m_card) {
        return;
    }
    const QString readerId = m_card->readerId(); // the reader the window binds to
    // CommandLauncherJob (vs ApplicationLauncherJob's URL-only surface) takes an
    // explicit executable + argument list, so `--reader <id>` passes cleanly,
    // AND it supplies the startup-notification / Wayland activation token. The
    // window is KDBusService::Unique: a second launch raises + re-targets
    // the running instance, its argv reaching the window via activateRequested.
    auto* job =
        new KIO::CommandLauncherJob(QString::fromLatin1(kCredentialsExe), credentialsLaunchArgs(readerId), this);
    job->setDesktopName(QStringLiteral("org.librescrs.credentials")); // startup-notify id
    job->start();
}

void SmartCardHandler::setReaderName(const QString& name)
{
    if (m_readerName == name) {
        return;
    }
    m_readerName = name;
    Q_EMIT readerNameChanged();
}

void SmartCardHandler::setCardLabel(const QString& label)
{
    if (m_cardLabel == label) {
        return;
    }
    m_cardLabel = label;
    Q_EMIT cardLabelChanged();
}

void SmartCardHandler::transitionTo(CardStateModel::State newState)
{
    if (newState == m_state) {
        return;
    }
    m_state = newState;
    Q_EMIT stateChanged();
}

void SmartCardHandler::setError(const QString& diagnosticOrEmpty)
{
    if (m_errorMessage == diagnosticOrEmpty) {
        return;
    }
    m_errorMessage = diagnosticOrEmpty;
    Q_EMIT errorMessageChanged();
}

QString SmartCardHandler::installHelpUrl()
{
    return Client::AgentClient::installHelpUrl();
}

void SmartCardHandler::setAgentInstalled(bool installed)
{
    if (m_agentInstalled == installed) {
        return;
    }
    m_agentInstalled = installed;
    Q_EMIT agentInstalledChanged();
}

void SmartCardHandler::rebuildIdentityModel(const QList<Client::FieldGroup>& groups)
{
    // Reuse the shared row assembly — the SAME rule the card:/ KIO worker
    // uses: the client library separates each security check's several
    // `check_<N>_<suffix>` wire fields (and reads the joined shape too), and
    // flattens everything else. It changes row count, so it happens BEFORE the
    // per-row loop. The plasmoid additionally drops empty-value rows (a blank
    // summary/expander row looks broken in a popup) and adapts to QVariantMap
    // for the QML Repeater.
    QVariantList flat;
    for (const Client::IdentityRow& row : LibreKDE::identityRows(groups)) {
        if (row.value.isEmpty() || LibreKDE::isHiddenIdentityRow(row)) {
            continue;
        }
        QVariantMap out;
        out.insert(QStringLiteral("groupKey"), row.groupKey);
        out.insert(QStringLiteral("fieldKey"), row.fieldKey);
        // Localize label AND value via the host's own tables — the SAME rules
        // the card:/ worker uses, falling back to the agent's English label.
        out.insert(QStringLiteral("label"), LibreKDE::localizedFieldLabel(row));
        out.insert(QStringLiteral("value"), LibreKDE::localizedFieldValue(row));
        flat.append(out);
    }

    m_identityFields = flat;
    m_identitySummary = curateIdentitySummary(flat);
    m_identityDetails = applyGroupHeadings(applyFieldOrder(curateIdentityDetails(flat, m_identitySummary)));
    Q_EMIT identityChanged();
}

QVariantList SmartCardHandler::applyFieldOrder(QVariantList details)
{
    // Permute WITHIN each group and write the rows back into the slots that
    // group already occupied, so cross-group layout is untouched and the
    // heading pass still sees the list it expects.
    //
    // Deliberately not one std::stable_sort over the whole list with a
    // comparator that calls rows of different groups equivalent: equivalence
    // has to be transitive, and two rows of one group separated by a row of
    // another would break that — undefined behaviour, not merely a bad order.
    QHash<QString, QList<qsizetype>> slotsByGroup;
    for (qsizetype i = 0; i < details.size(); ++i) {
        slotsByGroup[details.at(i).toMap().value(QStringLiteral("groupKey")).toString()].append(i);
    }

    for (auto it = slotsByGroup.cbegin(); it != slotsByGroup.cend(); ++it) {
        const QStringList order = LibreKDE::fieldOrderForGroup(it.key());
        if (order.isEmpty()) {
            continue; // no declared reading order: delivery order stands
        }
        const QList<qsizetype>& slots = it.value();
        QVariantList rows;
        rows.reserve(slots.size());
        for (const qsizetype slot : slots) {
            rows.append(details.at(slot));
        }
        const auto rank = [&order](const QVariant& entry) {
            const qsizetype at = order.indexOf(entry.toMap().value(QStringLiteral("fieldKey")).toString());
            return at < 0 ? order.size() : at; // unnamed keys keep their order, after the named ones
        };
        std::stable_sort(rows.begin(), rows.end(),
                         [&rank](const QVariant& a, const QVariant& b) { return rank(a) < rank(b); });
        for (qsizetype i = 0; i < slots.size(); ++i) {
            details[slots.at(i)] = rows.at(i);
        }
    }
    return details;
}

QVariantList SmartCardHandler::applyGroupHeadings(QVariantList details)
{
    // Group by IDENTITY, not by adjacency — the same rule renderIdentityTxt
    // follows, and for the same reason: a run-length pass prints a group's
    // heading twice the moment a list revisits that group, and leaves the
    // revisited rows sitting under a heading they do not belong to.
    QStringList groupOrder;
    QHash<QString, QVariantList> rowsByGroup;
    for (const QVariant& entry : std::as_const(details)) {
        const QString key = entry.toMap().value(QStringLiteral("groupKey")).toString();
        if (!rowsByGroup.contains(key)) {
            groupOrder << key;
        }
        rowsByGroup[key].append(entry);
    }

    QVariantList out;
    out.reserve(details.size());
    for (const QString& key : std::as_const(groupOrder)) {
        // Resolved per call, never cached: the heading has to follow a runtime
        // language change like every other string on this surface.
        const QString heading = LibreKDE::localizedGroupLabel(key);
        bool first = true;
        for (const QVariant& entry : std::as_const(rowsByGroup[key])) {
            QVariantMap row = entry.toMap();
            row.insert(QStringLiteral("groupHeading"), first ? heading : QString());
            out.append(row);
            first = false;
        }
    }
    return out;
}

namespace {
/// A field row's identity for the summary/details split: which group it
/// belongs to and which field within that group. Two rows can share
/// rendered text without sharing this (an X.509 key-usage row and a
/// certificate-purpose row can both read "Digital Signature").
using FieldIdentity = std::pair<QString, QString>;

FieldIdentity identityOf(const QVariantMap& row)
{
    return {row.value(QStringLiteral("groupKey")).toString(), row.value(QStringLiteral("fieldKey")).toString()};
}
} // namespace

QVariantList SmartCardHandler::curateIdentityDetails(const QVariantList& fields, const QVariantList& summary)
{
    // The popup renders the summary AND, once expanded, this list. They must
    // therefore partition the model: anything in both is printed twice on
    // screen, which is exactly how a summarised row like the card type came to
    // appear under its own heading and again in the expanded list.
    //
    // Subtract by (groupKey, fieldKey) — the row's identity — rather than by
    // its rendered text. `curateIdentitySummary` takes at most ONE row per
    // curated key, so a key emitted under two groups must keep the copy the
    // summary did not take; text matching would drop both.
    //
    // Neither `fields` nor `summary` can repeat an identity, on the path
    // this is actually built from: identity crosses the wire as a map of
    // maps (IdentityFieldGroupWire = QMap<QString, IdentityFieldWire>,
    // LibreAgent/client/qt/src/dbus/WireShapes.h), so `fields` can no more
    // hold two rows for the same (groupKey, fieldKey) than a QMap can hold
    // two values under one key, and `curateIdentitySummary` is itself
    // deduplicated (both its curated-key branch and its fallback) so it
    // never manufactures a duplicate `fields` does not have. A QSet is
    // therefore exactly the right structure for `summarised` below: there
    // is nothing here that ever needs to count past one.
    QSet<FieldIdentity> summarised;
    summarised.reserve(summary.size());
    for (const QVariant& entry : summary) {
        summarised.insert(identityOf(entry.toMap()));
    }

    QVariantList details;
    details.reserve(fields.size());
    for (const QVariant& entry : fields) {
        // Erase on first match, not "assert exactly one": belt-and-suspenders
        // for the same reason as above, not a case today's data can reach.
        if (summarised.remove(identityOf(entry.toMap()))) {
            continue;
        }
        details.append(entry);
    }
    return details;
}

QVariantList SmartCardHandler::curateIdentitySummary(const QVariantList& fields)
{
    // The identifying fields, in preferred display order. These are the ACTUAL
    // field keys the agent's LibreMiddleware identity plugins emit (verified
    // against the shipping code, not guessed):
    //   - rs-eid-plugin/src/eid_card_plugin.cpp: surname, given_name,
    //     personal_number, document_type, document_serial_number, doc_reg_no,
    //     expiry_date, card_type
    //   - rs-health-plugin/src/health_card_plugin.cpp: family_name, given_name,
    //     personal_number, card_id, date_of_expiry, valid_until
    //   - emrtd-plugin/src/emrtd_card_plugin.cpp: full_name, surname,
    //     given_names, personal_number, document_number, date_of_expiry
    //   - eu-vrc-plugin/src/eu_vrc_card_plugin.cpp: document_number,
    //     expiry_date
    // First match wins; card-agnostic (invents no field, uses the agent's own
    // labels).
    //
    // full_name is deliberately ABSENT here: it is the eMRTD DG11 row —
    // free-form supplementary data (often national-script, on some documents
    // not a person name at all). The machine-verified DG1/MRZ name components
    // are the primary identity source; full_name joins only as a FALLBACK
    // headline when no name component exists (see below).
    static const QStringList kSummaryKeys = {
        QStringLiteral("surname"),
        QStringLiteral("family_name"),
        QStringLiteral("given_name"),
        QStringLiteral("given_names"),
        QStringLiteral("personal_number"),
        QStringLiteral("document_number"),
        QStringLiteral("document_serial_number"),
        QStringLiteral("doc_reg_no"),
        QStringLiteral("card_id"),
        QStringLiteral("document_type"),
        QStringLiteral("card_type"),
        QStringLiteral("date_of_expiry"),
        QStringLiteral("expiry_date"),
        QStringLiteral("valid_until"),
    };
    static const QStringList kNameComponentKeys = {
        QStringLiteral("surname"),
        QStringLiteral("family_name"),
        QStringLiteral("given_name"),
        QStringLiteral("given_names"),
    };
    // Single pass; a name component counts only with a non-empty value, so
    // the rule also holds for callers that feed unfiltered flattened rows
    // (the plasmoid model drops empty-value rows earlier, card:/-style
    // consumers do not).
    // One emptiness rule for both the suppression predicate and the row
    // selection below, so an empty-valued name row can neither suppress
    // full_name nor headline the summary itself (callers that feed
    // unfiltered flattened rows retain empty values by contract).
    const auto hasText = [](const QVariantMap& row) {
        return !row.value(QStringLiteral("value")).toString().trimmed().isEmpty();
    };
    const bool hasNameComponent = std::any_of(fields.cbegin(), fields.cend(), [&hasText](const QVariant& entry) {
        const QVariantMap row = entry.toMap();
        return kNameComponentKeys.contains(row.value(QStringLiteral("fieldKey")).toString()) && hasText(row);
    });
    QStringList effectiveKeys = kSummaryKeys;
    if (!hasNameComponent) {
        effectiveKeys.prepend(QStringLiteral("full_name"));
    }
    // The summary row carries the source row's IDENTITY (groupKey/fieldKey)
    // alongside the two fields the delegate renders. The delegate reads only
    // label/value, but `curateIdentityDetails` has to subtract these rows from
    // the full model, and rendered text is not an identity: a key that appears
    // under two groups renders the same string twice.
    const auto toSummaryRow = [](const QVariantMap& row) {
        QVariantMap out;
        out.insert(QStringLiteral("groupKey"), row.value(QStringLiteral("groupKey")));
        out.insert(QStringLiteral("fieldKey"), row.value(QStringLiteral("fieldKey")));
        out.insert(QStringLiteral("label"), row.value(QStringLiteral("label")));
        out.insert(QStringLiteral("value"), row.value(QStringLiteral("value")));
        return out;
    };
    QVariantList summary;
    for (const QString& key : effectiveKeys) {
        for (const QVariant& entry : std::as_const(fields)) {
            const QVariantMap row = entry.toMap();
            if (row.value(QStringLiteral("fieldKey")).toString() == key && hasText(row)) {
                summary.append(toSummaryRow(row));
                break;
            }
        }
    }
    // Never empty: a card whose keys are outside the curated set still gets a
    // summary (the first few rows), so the headline is always populated.
    //
    // Deduplicated by identity, same as the curated branch above: this walks
    // `fields` in raw delivery order rather than by a fixed key list, so
    // nothing here guarantees a repeat couldn't reach it the way the
    // curated branch's fixed, distinct key list does. `curateIdentityDetails`
    // relies on `summary` never repeating an identity (a QSet, not a
    // multiset — see its own comment); this keeps that true regardless of
    // how `fields` was built, rather than resting the guarantee entirely on
    // the wire shape upstream.
    if (summary.isEmpty()) {
        constexpr int kFallbackRows = 4;
        QSet<FieldIdentity> seen;
        for (const QVariant& entry : std::as_const(fields)) {
            const QVariantMap row = entry.toMap();
            if (seen.contains(identityOf(row))) {
                continue;
            }
            seen.insert(identityOf(row));
            summary.append(toSummaryRow(row));
            if (summary.size() >= kFallbackRows) {
                break;
            }
        }
    }
    return summary;
}

void SmartCardHandler::clearIdentity()
{
    if (m_identityFields.isEmpty() && m_identitySummary.isEmpty()) {
        return;
    }
    m_identityFields.clear();
    m_identitySummary.clear();
    m_identityDetails.clear();
    Q_EMIT identityChanged();
}

QString SmartCardHandler::locateLibreCelik()
{
    // Generic, cross-distro "is LibreCelik installed, and where?" resolution.
    // Returns a launchable executable path, or empty when absent.
    //
    // 1) PATH — LibreCelik's desktop entry ships `Exec=LibreCelik`, so probe
    //    that exact (capitalised) name first; a lowercase fallback covers any
    //    package that renames the binary. (The previous lowercase-only probe
    //    missed the shipped `LibreCelik` binary on case-sensitive filesystems.)
    for (const QString& name : {QStringLiteral("LibreCelik"), QStringLiteral("librecelik")}) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty()) {
            return path;
        }
    }
    // 2) Freedesktop application database — the canonical, distro-agnostic
    //    registry of installed apps (native packages register `librecelik.desktop`
    //    even when the binary is not on this process's PATH). Resolve the
    //    registered Exec's binary to a full path so the launch path stays
    //    uniform (QProcess on that binary). The Exec FIELD itself decides
    //    launchability: a wrapper line ("flatpak run …", "env VAR=x …",
    //    "sh -c …") has an executable FIRST token, so resolving that token
    //    would mis-detect the wrapper as LibreCelik — and later launch e.g.
    //    `flatpak <readerName>`, a silently dead button. Only an Exec whose
    //    first token IS the app binary (basename `librecelik`, any case) is
    //    directly launchable; any other Exec is treated as absent rather than
    //    mis-launched — launch-by-KService can be added if wrapper packaging
    //    is ever shipped.
    if (const KService::Ptr service = KService::serviceByDesktopName(QStringLiteral("librecelik"))) {
        const QStringList execArgs = KShell::splitArgs(service->exec());
        const QString firstToken = execArgs.isEmpty() ? QString{} : execArgs.constFirst();
        if (QFileInfo(firstToken).fileName().compare(QLatin1String("librecelik"), Qt::CaseInsensitive) == 0) {
            const QString path = QStandardPaths::findExecutable(firstToken);
            if (!path.isEmpty()) {
                return path;
            }
        }
    }
    return {};
}

QString SmartCardHandler::readerSerialKey(const QString& name)
{
    // The unit serial is the last parenthesized group of the reader name
    // ("Gemalto PC Twin Reader (69988A87) 00 00" -> "69988A87";
    //  "... [OMNIKEY 5422CL Smartcard Reader] (IM0O2C00NF10456904) 01 00"
    //  -> "IM0O2C00NF10456904"). Qualifying needs a few characters AND at
    // least one digit: driver databases ship model-static parenthesized
    // tokens — "(1)", "(CCID)", "(ICCD)", "(Liteon)" — that are identical for
    // every unit of those models and must never become a matching key. A
    // model-static token that happens to carry digits (e.g. "(0013)") still
    // qualifies; sameReaderUnit() documents that residual risk.
    const int close = name.lastIndexOf(QLatin1Char(')'));
    if (close < 1) {
        return {};
    }
    const int open = name.lastIndexOf(QLatin1Char('('), close - 1);
    if (open < 0) {
        return {};
    }
    const QString key = name.mid(open + 1, close - open - 1).trimmed();
    if (key.size() < 4) {
        return {};
    }
    const bool hasDigit = std::any_of(key.cbegin(), key.cend(), [](QChar c) { return c.isDigit(); });
    return hasDigit ? key : QString{};
}

QString SmartCardHandler::readerBaseName(const QString& name)
{
    // The name minus its volatile parts: the last parenthesized group (the
    // serial-key candidate) and the trailing pcsc-lite " NN NN" enumeration
    // indices (two two-digit hex tokens appended after the serial). What
    // remains identifies the MODEL — the part that must agree before a shared
    // serial key may equate two names.
    QString base = name;
    const int close = base.lastIndexOf(QLatin1Char(')'));
    if (close >= 1) {
        const int open = base.lastIndexOf(QLatin1Char('('), close - 1);
        if (open >= 0) {
            base.remove(open, close - open + 1);
        }
    }
    static const QRegularExpression slotIndexToken(QStringLiteral("^[0-9A-Fa-f]{2}$"));
    QStringList tokens = base.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int stripped = 0; stripped < 2 && !tokens.isEmpty(); ++stripped) {
        if (!slotIndexToken.match(tokens.constLast()).hasMatch()) {
            break;
        }
        tokens.removeLast();
    }
    return tokens.join(QLatin1Char(' '));
}

bool SmartCardHandler::sameReaderUnit(const QString& a, const QString& b)
{
    // The same physical unit re-enumerated: BOTH names carry a qualifying unit
    // serial, the serials agree, AND the base name (the model part) agrees.
    // The base gate keeps a shared or model-static token from equating
    // DIFFERENT devices: two models with the same generic token (G&D Star Sign
    // 350 vs 550), and the two interfaces of a dual-interface reader (contact
    // vs contactless expose one USB serial under different bracketed model
    // strings). Residual risk: a digit-bearing model-static token (e.g.
    // "(0013)") equates two same-model UNITS — reachable only when the bound
    // unit is absent from the roster and a same-model twin is present, where
    // exact matching would have shown "not connected" instead.
    const QString keyA = readerSerialKey(a);
    if (keyA.isEmpty() || keyA != readerSerialKey(b)) {
        return false;
    }
    return readerBaseName(a) == readerBaseName(b);
}

QUrl SmartCardHandler::cardUrlForReader(const QString& readerName)
{
    QUrl url;
    url.setScheme(QStringLiteral("card"));
    url.setPath(QLatin1Char('/') + readerName);
    return url;
}

QString SmartCardHandler::plainDisplay(const QString& text)
{
    // The shared rule (see the header note and DisplayText.h): one rich-text
    // neutralizer for every host surface, re-exposed here as the QML seam.
    return DisplayText::plainDisplay(text);
}

void SmartCardHandler::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    Q_EMIT busyChanged();
}

QString SmartCardHandler::operationPhaseLabel(int phase) const
{
    switch (static_cast<Client::OperationPhase>(phase)) {
    case Client::OperationPhase::Reading:
        return ki18nc("@info:status reading data from the card", "Reading card…").toString();
    case Client::OperationPhase::AwaitingConsent:
        return ki18nc("@info:status waiting for PIN/CAN in the secure prompt", "Waiting for input…").toString();
    case Client::OperationPhase::Authenticating:
        return ki18nc("@info:status verifying the entered secret on the card", "Verifying…").toString();
    case Client::OperationPhase::Signing:
        return ki18nc("@info:status producing the signature on the card", "Signing…").toString();
    case Client::OperationPhase::Timestamping:
        return ki18nc("@info:status attaching a trusted timestamp", "Adding timestamp…").toString();
    case Client::OperationPhase::Created:
    case Client::OperationPhase::Connecting:
    case Client::OperationPhase::Done:
        break;
    }
    return ki18nc("@info:status generic in-progress card operation", "Working…").toString();
}

void SmartCardHandler::setOperationPhase(Client::OperationPhase phase)
{
    const int p = static_cast<int>(phase);
    if (m_operationPhase == p) {
        return;
    }
    m_operationPhase = p;
    Q_EMIT operationPhaseChanged();
}

void SmartCardHandler::copyField(const QString& value)
{
    if (value.isEmpty()) {
        return;
    }
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(value);
    }
}

bool SmartCardHandler::savePhoto(const QUrl& destination)
{
    if (m_photoBytes.isEmpty() || !destination.isValid()) {
        return false;
    }
    const QString path = destination.isLocalFile() ? destination.toLocalFile() : destination.path();
    if (path.isEmpty()) {
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(LibreKDE::Plasmoid::Logging) << "savePhoto: cannot open" << path;
        return false;
    }
    if (file.write(m_photoBytes) != m_photoBytes.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

void SmartCardHandler::openInFiles()
{
    if (m_readerName.isEmpty()) {
        return;
    }
    // Warm the certificate read BEFORE launching the file manager: the plasmoid
    // read identity, not certs, so the `card:/…/PKI` folder would otherwise pay a
    // cold cert read (PACE + eMRTD secure-channel reads, ~5-10 s) on first open.
    // This pre-reads them on the already-warm PACE session while Dolphin starts
    // (asynchronously — the launch below is never delayed); the agent dedups +
    // caches, so by the time the user navigates to PKI it is instant (and the
    // KIO worker's read shares the one card read).
    warmCertificateCache();
    // card:/<reader> is a KIO-registered scheme -> the default file manager
    // (Dolphin) handles it. QDesktopServices never routes a registered KIO
    // scheme to a browser.
    QDesktopServices::openUrl(cardUrlForReader(m_readerName));
}

void SmartCardHandler::startAgent()
{
    // A click on "Start service" lands here. The start waits for the agent in
    // local event loops, and the agent's arrival replaces the view that holds
    // the button (a Loader swaps the state component), so a start run under
    // the click would delete the button beneath its own click handler. The
    // client library runs it from the event loop instead and answers with
    // agentStartFinished. A second click before then starts nothing.
    if (m_agentStarting) {
        return;
    }
    m_agentStarting = true;
    Q_EMIT agentStartingChanged();
    // The bus starts the agent (its activation file names the systemd unit),
    // answers whether that worked, and needs no systemctl on PATH. Reaching the
    // agent fires availabilityChanged(true), which drives refresh().
    m_client->requestAgentStartLater();
}

void SmartCardHandler::onAgentStartFinished(Client::AgentStartResult result)
{
    // The client is shared by every widget in the process: only the widget that
    // asked reports the answer.
    if (!m_agentStarting) {
        return;
    }
    // `Requested` ends this request too: on Linux it means another start was
    // already waiting, and that one reports through availabilityChanged. Its
    // failure reaches no one here, so holding the control disabled until the
    // agent arrives could hold it disabled for good.
    m_agentStarting = false;
    Q_EMIT agentStartingChanged();
    const bool failed = result == Client::AgentStartResult::Failed;
    if (failed) {
        qCWarning(LibreKDE::Plasmoid::Logging) << "startAgent: the installed agent could not be started";
    }
    if (m_agentStartFailed != failed) {
        m_agentStartFailed = failed;
        Q_EMIT agentStartFailedChanged();
    }
    // A start that found nothing installed changes the guidance, not the state.
    refresh();
}

void SmartCardHandler::setOperationStallBudget(int milliseconds)
{
    m_opStallBudgetMs = milliseconds;
}

void SmartCardHandler::requestRefresh()
{
    // Manual re-check. refreshDiscovery() re-probes the bus name and re-runs
    // GetManagedObjects — recovering a card the agent exported but whose
    // InterfacesAdded we never received, or an agent that reappeared without a
    // watcher signal — and emits readersChanged(), which is wired to refresh(),
    // which re-reads whether the agent is installed. An installed agent that is
    // not running is started first: this is the user's "try again".
    //
    // Always from the event loop, never under the click: the refresh waits in
    // local event loops, and whatever they deliver (the agent coming up, a card
    // inserted) can swap the view that holds the button and delete it beneath
    // its own click handler -- with a running agent as much as a stopped one.
    m_client->refreshDiscoveryLater();
}

} // namespace LibreKDE::Plasmoid
