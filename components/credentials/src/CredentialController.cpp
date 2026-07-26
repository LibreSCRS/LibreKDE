// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "CredentialController.h"

#include "AgentCapabilities.h"
#include "AgentCard.h"
#include "AgentClient.h"
#include "AgentOperation.h"
#include "AgentReader.h"
#include "CredentialText.h"
#include "SharedAgentClient.h"

#include <KLocalizedString>

#include <QLatin1StringView>
#include <QLoggingCategory>
#include <QTextDocument> // Qt::mightBeRichText

#include <utility>

namespace {
Q_LOGGING_CATEGORY(lcCredentials, "librekde.credentials.controller")
} // namespace

namespace LibreKDE::Credentials {

CredentialController::CredentialController(QObject* parent)
    : CredentialController(LibreKDE::sharedAgentClient(), parent)
{}

CredentialController::CredentialController(std::shared_ptr<LibreKDE::AgentClient> client, QObject* parent)
    : QObject(parent), m_client(std::move(client))
{
    // The model must exist before the first refresh() (classify may populate /
    // clear it) and for the whole controller lifetime (CONSTANT property).
    m_model = new CredentialModel(this);
    wireClient();
    // Defer the FIRST classify to the event loop: the launcher applies the
    // explicit --reader target via bindReader() only AFTER construction (the
    // QML engine builds this object during load; main() binds afterwards, still
    // before exec()). A synchronous first refresh would let the first-reader-
    // with-card fallback probe the first-by-path card in that window — on a multi-reader
    // machine a wrong-card ListCredentials whose CAN prompt this window could
    // never dismiss. Deferred, an explicit bind always lands first and wins; a
    // no-arg launch still reaches the fallback on the first loop turn.
    QMetaObject::invokeMethod(this, &CredentialController::refresh, Qt::QueuedConnection);
}

CredentialController::~CredentialController() = default;

void CredentialController::wireClient()
{
    if (!m_client) {
        return;
    }
    // Any registry change re-resolves the bound reader/card and re-classifies.
    connect(m_client.get(), &LibreKDE::AgentClient::readersChanged, this, &CredentialController::refresh);
    connect(m_client.get(), &LibreKDE::AgentClient::cardChanged, this, [this](const QString&) { refresh(); });
    connect(m_client.get(), &LibreKDE::AgentClient::availabilityChanged, this, [this](bool) { refresh(); });
}

void CredentialController::bindReader(const QString& readerPath)
{
    // Always refresh — a re-bind of the SAME path is a legitimate re-target (a
    // second window launch raises the existing one and re-classifies, since the
    // card may have been swapped while the window was hidden).
    //
    // An explicit re-bind that targets a DIFFERENT reader than the one the
    // binding currently resolves to is user intent to abandon the in-flight
    // verb: detach it here so refresh()'s mutation guard no longer defers the
    // re-target. A re-bind of the reader ALREADY being managed (same path, or
    // the explicit form of the current fallback) keeps the running verb — a
    // second launch must never cancel the user's own PIN change on that card.
    if (m_mutationOp != nullptr && readerPath != m_resolvedReaderPath) {
        detachMutation();
        // The detach abandons the verb's mandatory post-mutation re-list — and
        // when the new path is stale/unresolvable, refresh()'s fallback can
        // re-resolve to the SAME card: bindCard() then short-circuits (no
        // resetListState) and startListCredentials() early-returns on the
        // settled latch, so NO transition would ever fire — the window would
        // strand in Working with a focus-trapped scrim and a dead Cancel.
        // Clearing the list state HERE forces re-classification past the latch
        // on every re-target; the re-fetch is correct regardless, since the
        // aborted mutation may have reached the card and invalidated the
        // agent's listing cache. Only this EXPLICIT user re-bind detaches —
        // registry events stay deferred by refresh()'s mutation guard.
        resetListState();
    }
    m_readerPath = readerPath;
    refresh();
}

void CredentialController::refresh()
{
    // Availability first: an unreachable agent is a client-level state, not a
    // NoCard. isAvailable() is a cached, non-blocking flag.
    if (!m_client || !m_client->isAvailable()) {
        m_resolvedReaderPath.clear();
        bindCard(nullptr);
        setReaderName({});
        transitionTo(State::AgentUnavailable);
        return;
    }

    // A live mutation PINS the current binding: a registry event must not
    // re-resolve/re-target it mid-verb. With a fallback binding, a card
    // inserted in another reader (or a stale bound reader reappearing) would
    // re-target bindCard(), whose detachMutation() silently cancels the user's
    // in-flight PIN change and dismisses the secure prompt. Deferring is safe:
    // the client's death/removal sweeps terminalize the op FIRST (clearing
    // m_mutationOp in onMutationDone), so the registry event that follows a
    // genuine card loss re-resolves normally, and everything else is covered by
    // the mandatory post-mutation re-list / the next registry event. An
    // explicit re-target detaches in bindReader() before reaching this guard.
    if (m_mutationOp != nullptr) {
        return;
    }

    LibreKDE::AgentReader* reader = m_readerPath.isEmpty() ? nullptr : m_client->reader(m_readerPath);
    if (reader == nullptr) {
        // A missing or stale --reader falls back to the first reader
        // holding a card, so the window manages the obvious card instead of
        // dead-ending on NoCard. The explicit binding stays sticky (m_readerPath
        // is untouched): should the bound reader (re)appear, it wins again on
        // the next registry event.
        reader = m_client->firstReaderWithCard();
    }
    if (reader == nullptr) {
        // Unbound with no fallback target: no card to manage anywhere.
        m_resolvedReaderPath.clear();
        bindCard(nullptr);
        setReaderName({});
        transitionTo(State::NoCard);
        return;
    }

    m_resolvedReaderPath = reader->path();
    setReaderName(reader->name());
    LibreKDE::AgentCard* card = m_client->card(reader->cardPath());
    bindCard(card);
    classify();
}

void CredentialController::classify()
{
    if (m_card == nullptr) {
        resetListState();
        m_model->setRecords({});
        transitionTo(State::NoCard);
        return;
    }
    // A card that does not advertise PIN management has no surface here (identity
    // reading / signing live in the plasmoid + Purpose plugin, not this window).
    if (!LibreKDE::has(m_card->capabilities(), LibreKDE::Cap::PinManagement)) {
        resetListState();
        m_model->setRecords({});
        transitionTo(State::NotManageable);
        return;
    }
    // Manageable: fetch the credential list (once per bound card) and let its
    // result drive Loading → Ready/Empty.
    startListCredentials();
}

void CredentialController::startListCredentials()
{
    // The mandatory post-mutation re-list (or an incidental re-classify) can run
    // after the bound card was unbound (a mid-mutation re-target or removal
    // race): with no card there is nothing to list — mirror classify()'s null
    // branch instead of dereferencing the nulled QPointer.
    if (m_card == nullptr) {
        resetListState();
        m_model->setRecords({});
        transitionTo(State::NoCard);
        return;
    }
    // Never auto-start a list while a verb is in flight: the Loading transition
    // would drop the Working scrim mid-mutation and two ops would run
    // concurrently. The mandatory relistAfterMutation() re-fetches once the
    // mutation lands (m_mutationOp is already null by then).
    if (m_mutationOp != nullptr) {
        return;
    }
    // Skip when a fetch is already in flight (m_listOp) or the list is settled
    // (a successful load, or a user-cancelled read we must not auto-re-prompt).
    // A card (re)bind clears both (resetListState), so a genuine swap/reinsert
    // re-fetches; a transient read error leaves m_listSettled false, so the next
    // card/availability event (or an explicit refresh()) re-fetches.
    if (m_listOp != nullptr || m_listSettled) {
        return;
    }
    LibreKDE::AgentOperation* op = m_card->listCredentials();
    if (op == nullptr) {
        // Method-entry throw (see AgentCard::lastCredentialError()): treat as a
        // transient read failure (NOT "unsupported") — left re-fetchable, since an
        // entry throw never reached a prompt, so re-attempting can't re-prompt. If a
        // post-mutation re-list is what threw, release the result-hold so ReadFailed
        // is rendered plainly (not held behind a stale banner).
        m_showingResult = false;
        m_model->setRecords({});
        transitionTo(State::ReadFailed);
        return;
    }
    m_listOp = op;
    // Suppress the Loading spinner while a just-finished verb's Result banner is
    // still up (the mandatory post-mutation re-list): the outcome must stay visible
    // until fresh rows land. Every other list fetch shows Loading normally.
    if (!m_showingResult) {
        transitionTo(State::Loading);
    }
    // Drive the whole outcome off the terminal `finished` alone. The Credentials
    // Result races with Finished (the fake and the agent emit it for EVERY
    // completed attempt, Ok AND error), so keying off credentialsResultReady would
    // be order-dependent; by `finished(Ok)` the credentials result is guaranteed
    // populated (AgentOperation's GetResult recovery runs before it, else the op
    // finishes Error), so reading it here is race-free.
    connect(op, &LibreKDE::AgentOperation::finished, this,
            [this, op](LibreKDE::OperationStatus status, LibreKDE::ErrorCode, const QString&, const QString&) {
                onListFinished(static_cast<int>(status), op);
            });
}

void CredentialController::onListFinished(int status, LibreKDE::AgentOperation* op)
{
    // Release the op-latch on EVERY terminal path so a later re-classify/refresh
    // is never blocked by a stale pointer (the op is parented to the card; we just
    // drop our reference — the QPointer auto-nulls when the card reaps it).
    m_listOp = nullptr;
    // Consume the cancel-request flag on EVERY terminal path (a too-late cancel
    // can still land Ok; the flag must not leak into the next fetch).
    const bool cancelRequested = m_listCancelRequested;
    m_listCancelRequested = false;
    // Fresh rows (or a failure) have now landed: release the result-hold so the
    // Ready/Empty/ReadFailed transition below actually clears the Result banner.
    m_showingResult = false;

    if (status == static_cast<int>(LibreKDE::OperationStatus::Ok)) {
        m_listSettled = true; // loaded — don't re-fetch on an incidental re-classify
        m_model->setRecords(op->credentialsResult());
        // An empty list is a manageable card with nothing to act on (distinct from
        // NotManageable — a card outside this window's scope).
        transitionTo(op->credentialsResult().isEmpty() ? State::Empty : State::Ready);
        return;
    }

    // A non-Ok read: the card is present but its list couldn't be read. Show the
    // neutral ReadFailed message (NOT "unsupported"). Distinguish intent:
    //   * user cancel  -> latch (re-listing after a cancelled CAN re-prompts, so
    //                     the user's choice to stop must stick until a card event).
    //                     Belt-and-braces like the mutation path: the request flag
    //                     is authoritative (a no-Result abort arrives rewritten as
    //                     Error/CommunicationError), a preserved Cancelled status
    //                     also counts;
    //   * transient err -> leave re-fetchable (next card/availability event or an
    //                     explicit refresh() retries) — no tight auto-retry loop,
    //                     since only discrete external events call classify().
    m_model->setRecords({});
    m_listSettled = cancelRequested || status == static_cast<int>(LibreKDE::OperationStatus::Cancelled);
    transitionTo(State::ReadFailed);
}

void CredentialController::resetListState()
{
    if (m_listOp != nullptr) {
        // Mirror detachMutation()'s ordering — stop listening BEFORE the
        // agent-side cancel, then reap. The fire-and-forget Cancel dismisses
        // any secure prompt the abandoned read may have raised (a re-target
        // must never leave an orphaned CAN prompt behind).
        m_listOp->disconnect(this);
        m_listOp->cancel();
        m_listOp->deleteLater(); // parented to the card; reap our fetch early
        m_listOp = nullptr;
    }
    m_listSettled = false;
    m_listCancelRequested = false;
    m_showingResult = false;
}

void CredentialController::bindCard(LibreKDE::AgentCard* card)
{
    // Short-circuit only when re-binding the SAME, still-live card. With
    // `card == nullptr` we must NOT short-circuit on `m_card == card`: a removed
    // card destroys the AgentCard, so the QPointer has already auto-nulled, and
    // `nullptr == nullptr` would skip disconnecting the (already-dead) card.
    if (card != nullptr && m_card == card) {
        return;
    }
    // Binding a different (or null) card: the previous card's list is stale, so
    // reap the in-flight fetch and mark not-loaded — the ensuing classify()
    // re-fetches for the new card. An in-flight MUTATION belongs to the previous
    // binding too: detach it, or its terminal would fire the old card's result
    // banner + mandatory re-list over the new target (and, with the card
    // unbound, deref a nulled QPointer).
    resetListState();
    detachMutation();
    if (m_card != nullptr) {
        m_card->disconnect(this);
    }
    m_card = card;
    if (m_card != nullptr) {
        // A live card supersedes any card-removed notice still on the NoCard
        // surface (the notice must NOT be cleared on the null bind — the
        // removal itself unbinds, and the notice's whole purpose is to explain
        // that NoCard).
        setRemovalNotice({});
        // A live capability change (e.g. the agent surfaces PinManagement after a
        // pre-read unlock) re-classifies the active card.
        connect(m_card, &LibreKDE::AgentCard::changed, this, &CredentialController::classify);
    }
}

void CredentialController::detachMutation()
{
    if (m_mutationOp != nullptr) {
        // Stop listening BEFORE cancelling: the aborted op's terminal must not
        // reach onMutationDone (it describes the previous binding). The agent-side
        // Cancel also dismisses any secure prompt still up for the old card.
        m_mutationOp->disconnect(this);
        m_mutationOp->cancel();
        m_mutationOp = nullptr;
    }
    m_mutationCancelRequested = false;
    m_pendingPresentedKind = LibreKDE::CredentialKind::Unknown;
}

void CredentialController::cancel()
{
    // Abort whatever is in flight. A cancelled LIST emits finished(Cancelled),
    // which onListFinished lands on ReadFailed AND latches (m_listSettled) so a
    // cancelled read is not silently re-attempted (re-listing after a cancelled CAN
    // would re-prompt). A cancelled MUTATION finishes with a userCancelled/
    // Unspecified outcome, which onMutationDone renders neutrally and then re-lists.
    // At most one is live at a time (Working ⇒ mutation, Loading ⇒ list), but a
    // re-list can be in flight while Result is shown — cancelling both is safe.
    if (m_mutationOp != nullptr) {
        // Latch the user's intent BEFORE the async Cancel: the aborted op may finish
        // Error/CommunicationError with no outcome (the agent delivered no
        // userCancelled Result), so onMutationDone can only know this was a cancel
        // from this flag — and a cancel must never render as a red error banner.
        m_mutationCancelRequested = true;
        m_mutationOp->cancel();
    }
    if (m_listOp != nullptr) {
        // Latch the user's intent BEFORE the async Cancel, mirroring the mutation
        // path: an aborted list the agent answers with NO Result arrives at
        // onListFinished as Error/CommunicationError (finalizeTerminal rewrites a
        // Cancelled terminal it cannot back with a payload), so the preserved
        // status alone cannot carry the cancel latch in that half.
        m_listCancelRequested = true;
        m_listOp->cancel();
    }
}

// --- Verb flow --------------------------------------------------------------
//
// Every verb is secret-free here: the agent's secure prompter collects the PIN/
// PUK/CAN. We only mint the mutation, reflect its typed PinResult, and run the
// mandatory post-mutation re-list. Attribution: the presented kind is
// stashed at invoke time, while the record it names is still in the model.

LibreKDE::CredentialKind CredentialController::kindOf(const QString& id) const
{
    const std::optional<LibreKDE::CredentialRecord> rec = m_model->recordById(id);
    return rec ? rec->kind : LibreKDE::CredentialKind::Unknown;
}

bool CredentialController::canStartMutation() const
{
    // A verb needs a bound (manageable) card and no verb already in flight — the
    // Working scrim blocks a second click in the UI, but the invokable is public,
    // so guard here too (never mint two overlapping mutations).
    return m_card != nullptr && m_mutationOp == nullptr;
}

void CredentialController::changePin(const QString& id)
{
    if (!canStartMutation()) {
        return;
    }
    m_pendingPresentedKind = kindOf(id);
    beginMutation(m_card->managePin(id, QStringLiteral("change"), {}), m_pendingPresentedKind);
}

void CredentialController::activate(const QString& id)
{
    if (!canStartMutation()) {
        return;
    }
    m_pendingPresentedKind = kindOf(id);
    // Bring the on-card signing key up in the SAME flow when it is pending, so the
    // user is not asked for the (spent) transport value a second time.
    const std::optional<LibreKDE::CredentialRecord> rec = m_model->recordById(id);
    const QVariantMap options{{QStringLiteral("activateKey"), rec && rec->keyActivationPending}};
    beginMutation(m_card->managePin(id, QStringLiteral("activate_pin"), options), m_pendingPresentedKind);
}

void CredentialController::activateSigningKey(const QString& id)
{
    Q_UNUSED(id) // the wire ActivateSigningKey is id-less; id only anchors the QML button
    if (!canStartMutation()) {
        return;
    }
    // Attribution: a standalone key activation presents the Signing PIN.
    m_pendingPresentedKind = LibreKDE::CredentialKind::Sign;
    beginMutation(m_card->activateSigningKey(), m_pendingPresentedKind);
}

void CredentialController::requestUnblock(const QString& id)
{
    // Don't raise the confirm sheet mid-mutation: a later confirmUnblock would
    // silently no-op on canStartMutation(), leaving a dead-end sheet. Gate the
    // pre-flight on the same condition as the verbs it leads to.
    if (!canStartMutation()) {
        return;
    }
    // Client-side pre-flight only: surface the PUK's remaining unblock budget and
    // launch the sheet. No card I/O here — the PUK is entered in the agent's
    // prompter after the user confirms (confirmUnblock).
    QString budget;
    const int pukRow = m_model->rowOfKind(LibreKDE::CredentialKind::Puk);
    if (pukRow >= 0) {
        const QString pukId = m_model->data(m_model->index(pukRow), CredentialModel::IdRole).toString();
        if (const auto puk = m_model->recordById(pukId)) {
            // The PUK's remaining unblock capacity is its usage budget ("a PUK
            // good for N unblocks", usesLeft/usesMax) — the count the holder
            // spends one of here. Attributed to the PUK (the sheet carries no
            // header). NOT the DOCP reset counter (unblocksLeft), which counts
            // retry-counter resets, not PIN unblocks.
            const QString who = LibreKDE::CredentialText::kindName(LibreKDE::CredentialKind::Puk);
            if (puk->usesLeft.has_value()) {
                budget = puk->usesMax.has_value()
                             ? ki18ndc("librekde", "unblock sheet: %1 PUK name, %2/%3 remaining/total unblocks",
                                       "%1: %2 of %3 unblocks left")
                                   .subs(who)
                                   .subs(*puk->usesLeft)
                                   .subs(*puk->usesMax)
                                   .toString()
                             : ki18ndc("librekde", "unblock sheet: %1 PUK name, %2 remaining unblocks",
                                       "%1: %2 unblocks left")
                                   .subs(who)
                                   .subs(*puk->usesLeft)
                                   .toString();
            }
        }
    }
    Q_EMIT unblockConfirmRequested(id, budget);
}

void CredentialController::confirmUnblock(const QString& id)
{
    if (!canStartMutation()) {
        return;
    }
    // Attribution: unblock presents the PUK, so the result's counters
    // describe the PUK — regardless of which PIN is being unblocked.
    m_pendingPresentedKind = LibreKDE::CredentialKind::Puk;
    beginMutation(m_card->managePin(id, QStringLiteral("unblock"), {}), m_pendingPresentedKind);
}

void CredentialController::beginMutation(LibreKDE::AgentOperation* op, LibreKDE::CredentialKind presented)
{
    m_pendingPresentedKind = presented;
    // A fresh verb obsoletes any earlier removal notice — INCLUDING one whose
    // recovery verb refuses at entry below: cleared BEFORE the entry-error
    // return, or a stale card-removed notice would survive under the new verb's
    // own Result surface.
    setRemovalNotice({});
    if (op == nullptr) {
        // Method-entry throw (UnknownCredential / RateLimited / …): no Operation was
        // minted — branch on the captured error name, never a red banner.
        handleEntryError();
        return;
    }
    // The verb was launched from the Result state while the previous mutation's
    // mandatory re-list was still in flight (its rows not yet landed). Detach that
    // re-list so its terminal doesn't clobber the Working state we're entering — the
    // new mutation runs its own re-list afterwards. (canStartMutation already barred
    // re-entry while a mutation op is live, so only a re-list can be in flight here.)
    if (m_listOp != nullptr) {
        resetListState();
    }
    m_mutationOp = op;
    m_mutationCancelRequested = false; // a fresh verb — no cancel requested yet
    setProgress(0.0);
    transitionTo(State::Working);
    // Drive the outcome off the terminal `finished` alone — NOT credentialsResultReady
    // (which races Finished). By `finished`, pinResult() is guaranteed populated (the
    // Credentials Result is delivered for every completed attempt, Ok AND soft-fail),
    // so reading it in onMutationDone is race-free. Connecting AFTER the mint is safe:
    // a synchronously-finished op queued its Finished during ctor recovery, delivered
    // once we return to the event loop.
    connect(op, &LibreKDE::AgentOperation::finished, this,
            [this, op](LibreKDE::OperationStatus, LibreKDE::ErrorCode, const QString&, const QString&) {
                onMutationDone(op);
            });
    connect(op, &LibreKDE::AgentOperation::phaseChanged, this,
            [this](LibreKDE::OperationPhase, double progress) { setProgress(progress); });
}

void CredentialController::onMutationDone(LibreKDE::AgentOperation* op)
{
    m_mutationOp = nullptr;
    const LibreKDE::CredentialOutcome outcome = op->pinResult().outcome;
    const LibreKDE::ErrorCode errorCode = op->errorCode();

    // A cancel is the user's OWN action — it must never render as a red error banner.
    // Detect it from either the request flag (authoritative: an aborted op the agent
    // didn't answer with a userCancelled Result finishes Error/CommunicationError
    // with `pinResult` at default Unspecified, so only the flag knows) OR a terminal
    // Cancelled status — EXCEPT when that status was manufactured by the client-side
    // death sweeps: AgentClient terminate()s live ops with Cancelled + CardRemoved
    // (card pulled) / CommunicationError (agent vanished). Those are external
    // terminations, not the user's choice, and deserve truthful feedback.
    const bool sweepTerminated =
        errorCode == LibreKDE::ErrorCode::CardRemoved || errorCode == LibreKDE::ErrorCode::CommunicationError;
    const bool cancelled =
        m_mutationCancelRequested || (op->status() == LibreKDE::OperationStatus::Cancelled && !sweepTerminated);
    m_mutationCancelRequested = false;

    // A card pulled mid-verb usually terminalizes client-side (the sweep) before the
    // agent's own Result(cardRemoved) can arrive, leaving the outcome Unspecified —
    // surface the spec's cardRemoved copy instead of the generic "did not complete".
    LibreKDE::CredentialOutcome effective = outcome;
    if (!cancelled && outcome == LibreKDE::CredentialOutcome::Unspecified &&
        errorCode == LibreKDE::ErrorCode::CardRemoved) {
        effective = LibreKDE::CredentialOutcome::CardRemoved;
    }

    // resultIsError: neutral (Ok / userCancelled) → not an
    // error; everything else (invalidPin, blocked, keyActivationFailed, …) → error —
    // EXCEPT a cancel is forced neutral regardless of the raw outcome. A partial
    // bring-up (pinActivated && !keyActivated ⇒ keyActivationFailed) needs no special
    // case: the message says the key step failed, and the re-list below will now
    // advertise keyActivatable, surfacing the standalone Activate button — we never
    // re-request the spent transport value.
    const bool isError =
        !cancelled && !LibreKDE::CredentialText::isNeutral(effective) && effective != LibreKDE::CredentialOutcome::Ok;
    // A cancel with no real outcome shows no banner; when a genuine outcome IS present
    // its text still renders (informationally — never red, since isError is false).
    const QString message =
        (cancelled && effective == LibreKDE::CredentialOutcome::Unspecified)
            ? QString()
            : LibreKDE::CredentialText::outcomeMessage(effective, m_pendingPresentedKind, op->pinResult().retriesLeft);
    setResult(isError, message);

    // A mutation terminated by a card pull has no dashboard to return to and no
    // card to re-list: the removal registry event that follows the sweep lands
    // NoCard, where the Result banner never renders. Carry the truthful outcome
    // on the NoCard surface instead (removalNotice — the placeholder's
    // explanation) and skip the mandatory re-list. Result is still entered so
    // the outcome renders even if the card object outlives the pull (e.g. the
    // agent reported cardRemoved while the registry still lists the card).
    if (effective == LibreKDE::CredentialOutcome::CardRemoved) {
        setRemovalNotice(message);
        transitionTo(State::Result);
        return;
    }

    transitionTo(State::Result);

    // Mandatory re-list: this mutation reached the card, so the agent invalidated its
    // listing cache. We ALWAYS re-list — including a neutral userCancelled, where the
    // re-list is harmless (the agent's secret cache is untouched, so the cached CAN is
    // reused and nothing re-prompts). Always re-listing is the simplest rule that
    // guarantees the listing-cache invalidation contract before any further action.
    relistAfterMutation();
}

void CredentialController::handleEntryError()
{
    const QString err = m_card != nullptr ? m_card->lastCredentialError() : QString();

    if (err.endsWith(QLatin1StringView("UnknownCredential"))) {
        // The agent dropped its listing cache (the id we sent is stale). Recover the
        // fresh ids with a re-list; show a neutral "refreshed" notice, never a red
        // error — the user did nothing wrong.
        setResult(false, ki18nd("librekde", "The card's credentials changed; the list was refreshed.").toString());
        transitionTo(State::Result);
        relistAfterMutation();
        return;
    }
    if (err.endsWith(QLatin1StringView("RateLimited"))) {
        // The request never reached the card, so the listing is still valid — no
        // re-list. A neutral "please wait" over the (still usable) dashboard; the user
        // retries when ready.
        setResult(false, ki18nd("librekde", "Please wait a moment before trying again.").toString());
        transitionTo(State::Result);
        return;
    }
    if (err.endsWith(QLatin1StringView("InvalidRequest"))) {
        // No longer a client-bug invariant: the agent maps a REAL, user-reachable
        // card condition — two records with identical labels (AmbiguousCredential) —
        // onto this wire error, so a user clicking Change on such a card lands here.
        // Log for diagnostics and surface a neutral notice; NO re-list — the
        // condition is persistent for the card, so a re-fetch cannot clear it. A
        // dedicated wire error for ambiguous credentials is the planned follow-up
        // once all four mirrors can be extended together; the client keeps handling
        // InvalidRequest gracefully regardless.
        qCWarning(lcCredentials).noquote() << "credential verb refused as InvalidRequest:" << err;
        setResult(false, ki18nd("librekde", "The card refused the request.").toString());
        transitionTo(State::Result);
        return;
    }
    // Any other entry refusal: a neutral generic notice plus a defensive re-list, so
    // the window recovers to a fresh, actionable dashboard rather than a red banner.
    qCWarning(lcCredentials).noquote() << "credential verb refused at method entry:" << err;
    setResult(false, ki18nd("librekde", "The action could not be started.").toString());
    transitionTo(State::Result);
    relistAfterMutation();
}

void CredentialController::relistAfterMutation()
{
    // Drop the settled latch + any stale list op, then re-fetch — but hold the Result
    // banner up (m_showingResult) so the just-computed outcome stays visible while the
    // fresh list loads underneath. onListFinished releases the hold and moves to
    // Ready/Empty (or ReadFailed) once new rows land, which is what finally clears the
    // banner. resetListState() clears m_showingResult, so it is set AFTER.
    resetListState();
    m_showingResult = true;
    startListCredentials();
}

QString CredentialController::plainDisplay(const QString& text)
{
    // See the header note: escape ONLY what AutoText would promote to
    // StyledText, so a hostile value renders as literal characters while every
    // legitimate value stays byte-identical.
    return Qt::mightBeRichText(text) ? text.toHtmlEscaped() : text;
}

void CredentialController::setResult(bool isError, const QString& message)
{
    if (m_resultIsError == isError && m_resultMessage == message) {
        return;
    }
    m_resultIsError = isError;
    m_resultMessage = message;
    Q_EMIT resultChanged();
}

void CredentialController::setProgress(double progress)
{
    if (qFuzzyCompare(m_progress, progress)) {
        return;
    }
    m_progress = progress;
    Q_EMIT progressChanged();
}

void CredentialController::setRemovalNotice(const QString& notice)
{
    if (m_removalNotice == notice) {
        return;
    }
    m_removalNotice = notice;
    Q_EMIT removalNoticeChanged();
}

void CredentialController::transitionTo(State next)
{
    if (next == m_state) {
        return;
    }
    m_state = next;
    Q_EMIT stateChanged();
}

void CredentialController::setReaderName(const QString& name)
{
    if (m_readerName == name) {
        return;
    }
    m_readerName = name;
    Q_EMIT readerNameChanged();
}

} // namespace LibreKDE::Credentials
