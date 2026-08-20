// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CardPhotoStore.h"
#include "CardStateModel.h"

#include <LibreSCRS/AgentClient/OperationPhase.h> // the phase the spinner status line renders
#include <LibreSCRS/AgentClient/Types.h>          // FieldGroup — the identity read's result shape

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

namespace LibreSCRS::AgentClient {
class AgentClient;
class AgentCard;
class AgentOperation;
class AgentReader;
} // namespace LibreSCRS::AgentClient

namespace LibreKDE {
class SignJob;
} // namespace LibreKDE

namespace LibreKDE::Plasmoid {

/// QML-facing controller. Registered as an instantiable QML type via
/// `qt_add_qml_module(URI org.librescrs.smartcard)`; each plasmoid instance
/// creates its OWN `SmartCard { }` at its `PlasmoidItem` root and reads
/// `smartCard.state`, `smartCard.readIdentity()`, etc.
///
/// NOT a QML singleton: Plasma 6 hosts EVERY applet instance in the single
/// `PlasmaQuick::globalEngine()`, so an engine-scoped singleton would alias
/// all widgets onto one handler and break the per-widget reader binding
/// (two widgets bound to different readers must render different
/// cards concurrently). All per-widget state (bound reader, master-detail
/// pick, identity model, photo slot) therefore lives per instance, while the
/// genuinely process-wide surfaces stay shared: the ONE `AgentClient`
/// (`sharedAgentClient()`, the session-bus connection + ObjectManager
/// discovery established once per process) and the ONE `CardPhotoStore`
/// (`sharedCardPhotoStore()`, keyed by per-handler photo slot).
///
/// A thin client of `org.librescrs.Agent`: NO card session, NO secrets.
/// Card I/O, PACE/PIN/MRZ entry and signing all live in the agent — its
/// prompter collects secrets when an operation needs them. The handler only
/// reflects reader/card state to QML and drives a read.
///
/// Single-threaded: all `AgentClient` signals arrive on the QML thread
/// (QtDBus dispatch).
class SmartCardHandler : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SmartCard)
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    /// True while an agent read is in flight (drives the loading placeholder).
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /// Whether LibreCelik was detected on the system (executable on PATH). QML
    /// hides the "Open in LibreCelik" affordance when false — never a dead button.
    Q_PROPERTY(bool libreCelikAvailable READ libreCelikAvailable CONSTANT)
    /// True when the active card advertises `Cap::PinManagement` — gates the
    /// "Manage credentials…" affordance (change / unblock PIN). Purely
    /// capability-bit-driven: computing it issues NO card read, and it is false
    /// whenever no card is bound. Carries its OWN change signal: transitionTo()
    /// dedupes stateChanged, so a live capability flip that keeps the coarse
    /// state (e.g. Hybrid stays Hybrid) would leave a stateChanged-riding
    /// binding stale.
    Q_PROPERTY(bool pinManagementAvailable READ pinManagementAvailable NOTIFY pinManagementAvailableChanged)
    /// True only in State::AgentUnavailable: whether the agent looks installed
    /// (D-Bus activation file / systemd unit present) but is merely stopped, vs
    /// not installed at all. Drives the AgentUnavailableState guidance.
    Q_PROPERTY(bool agentInstalled READ agentInstalled NOTIFY agentInstalledChanged)
    /// True in State::NoCard when a reader in scope physically reports a card
    /// (`Reader1.HasCard`) but no resolved `Card1` is available for it yet — the
    /// agent's deferred-publish window (it exports the card only after a worker
    /// resolves its capabilities, since the identity plugins ship empty ATR
    /// tables), or a genuinely dropped `InterfacesAdded`. Lets the QML say
    /// "detecting a card" instead of "insert a card" while one is actually
    /// seated, so a normal startup transient is never mistaken for "no card".
    Q_PROPERTY(bool cardDetected READ cardDetected NOTIFY cardDetectedChanged)
    Q_PROPERTY(QString readerName READ readerName NOTIFY readerNameChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString cardLabel READ cardLabel NOTIFY cardLabelChanged)
    /// True once a face photo has been read and decoded for the active card.
    Q_PROPERTY(bool hasCardPhoto READ hasCardPhoto NOTIFY cardPhotoChanged)
    /// Suggested "Save photo…" file name, with the extension derived from the
    /// RAW agent bytes' ACTUAL image format (savePhoto writes those raw bytes,
    /// which may be JPEG/JPEG2000 — never blindly ".png"). Empty when no photo.
    Q_PROPERTY(QString photoSuggestedFileName READ photoSuggestedFileName NOTIFY cardPhotoChanged)
    /// The QML `Image.source` URL for the active card's photo, resolved by the
    /// module's `CardPhotoProvider`: `image://librekde/cardphoto/<slot>?<token>`.
    /// The slot addresses THIS handler's entry in the shared store (per-widget
    /// isolation); the per-read token defeats the QML pixmap cache so a new
    /// card's photo always re-requests. Empty when no photo is available.
    Q_PROPERTY(QString cardPhotoUrl READ cardPhotoUrl NOTIFY cardPhotoChanged)
    /// True once a successful identity read has populated the model.
    Q_PROPERTY(bool hasIdentity READ hasIdentity NOTIFY identityChanged)
    /// The full identity, flattened for a QML Repeater: each entry is a QVariantMap
    /// { "groupKey", "fieldKey", "label", "value" } (binary fields skipped, matching
    /// the card:/ renderer). Drives the expander.
    Q_PROPERTY(QVariantList identityFields READ identityFields NOTIFY identityChanged)
    /// A curated subset of `identityFields`: the identifying fields (name /
    /// personal number / document number / type / expiry, in a fixed preferred
    /// order), each { "groupKey", "fieldKey", "label", "value" }. Falls back to
    /// the first rows so it is never empty on an unrecognized card. Drives the
    /// summary.
    Q_PROPERTY(QVariantList identitySummary READ identitySummary NOTIFY identityChanged)
    /// `identityFields` MINUS the rows `identitySummary` already shows — the
    /// rows the expander reveals. The two lists partition `identityFields`, so
    /// a view that renders the summary and this one shows every field exactly
    /// once; a view that renders the summary and `identityFields` shows every
    /// summarised field TWICE. Drives the expanded detail list.
    Q_PROPERTY(QVariantList identityDetails READ identityDetails NOTIFY identityChanged)
    /// True while an agent Sign started by `signFile()` is in flight ON THE
    /// READER CURRENTLY DISPLAYED (drives the "Sign a file…" button's
    /// busy/disabled state and a never-blank affordance). Reader-scoped, not
    /// widget-scoped: the Auto-mode chooser shows one chip per reader holding a
    /// card and therefore promises per-reader state, while a sign — unlike an
    /// identity read, which `bindCard()` cancels on a re-target — deliberately
    /// keeps running on the card it was started on. A widget-wide flag would
    /// spin the other cards' spinners and, worse, disable their "Sign a file…"
    /// too, making concurrent signing unreachable from this client.
    Q_PROPERTY(bool signingBusy READ signingBusy NOTIFY signingBusyChanged)
    /// Phase of the DISPLAYED reader's in-flight sign as an OperationPhase int;
    /// drives the sign spinner's status line. Reset to Created(0) when that
    /// reader starts a new sign. Reader-scoped for the same reason as
    /// `signingBusy`; kept separate from `operationPhase` because the two can
    /// now genuinely run at once (a read on the displayed card while another
    /// reader signs).
    Q_PROPERTY(int signPhase READ signPhase NOTIFY signPhaseChanged)
    /// The DISPLAYED reader's LAST sign outcome, as
    /// { "outcome": SignOutcome int, "outputPath", "certLabel", "level",
    /// "message" } — the sign result banner's whole source. State, not an
    /// event: a sign that finishes while another chip is displayed must not
    /// paint its banner over a foreign card, and must still be there when the
    /// user comes back to the card that signed. Empty-ish (outcome None) for a
    /// reader that has not signed, whose result was dismissed, or whose card
    /// has since been swapped.
    Q_PROPERTY(QVariantMap signResult READ signResult NOTIFY signResultChanged)
    /// Current phase of the in-flight identity READ as an OperationPhase int;
    /// drives the read spinner's status line (IdentityView / PreAuthState).
    /// Reset to Created(0) when a new read starts. A read belongs to the
    /// displayed card and is cancelled when the chip moves, so one scalar is
    /// the truth here; the sign's phase lives in `signPhase`.
    Q_PROPERTY(int operationPhase READ operationPhase NOTIFY operationPhaseChanged)
    /// The per-widget reader binding. Empty = "Auto" (follow the
    /// active card); a friendly reader Name pins the widget to that reader only.
    /// Driven one-way from `Plasmoid.configuration.boundReaderName` by main.qml.
    Q_PROPERTY(QString boundReaderName READ boundReaderName WRITE setBoundReaderName NOTIFY boundReaderNameChanged)
    /// True when the per-widget bound reader is currently on the bus (in the
    /// roster) — present, though possibly card-less. False in Auto mode and when
    /// the bound reader is absent (unplugged, or its arrival was missed). Drives
    /// NoCardState's honest "Insert a card" vs "Reader not connected" split: a
    /// refresh cannot conjure back a reader that isn't on the bus, so the
    /// affordance must not imply it can.
    Q_PROPERTY(bool boundReaderPresent READ boundReaderPresent NOTIFY boundReaderPresentChanged)
    /// Friendly Names of ALL readers the agent currently reports (present, with
    /// or without a card), sorted-path order — the config chooser's source.
    Q_PROPERTY(QStringList availableReaderNames READ availableReaderNames NOTIFY availableReaderNamesChanged)
    /// Friendly Names of the readers that currently hold a resolvable card,
    /// sorted-path order — the master list for the Auto multi-card view.
    Q_PROPERTY(QStringList readersWithCards READ readersWithCards NOTIFY readersWithCardsChanged)
    /// Whether this widget's content is currently VISIBLE (main.qml binds it to
    /// the popup's `expanded`). Drives the "free read" internally:
    /// the handler re-checks `ensureFreeRead()` whenever viewActive turns on
    /// AND whenever the active card is (re)classified while it is on — so a
    /// card that lands, gets chip-selected, or is swapped in the same reader
    /// while the popup is open populates even when neither the state nor the
    /// reader name changes (two same-classification cards fire no
    /// stateChanged). False (the default, incl. the config dialog's handler):
    /// no view, no read — identity/photo PII never loads for an unopened popup.
    Q_PROPERTY(bool viewActive READ viewActive WRITE setViewActive NOTIFY viewActiveChanged)
public:
    /// What a reader's last sign ended as — the `outcome` entry of
    /// `signResult`. Mirrored as named constants in SignFileAction.qml, the
    /// same way the card states are mirrored in CardActionBar.qml: no C++/QML
    /// enum plumbing for a presentation-only value.
    enum class SignOutcome {
        None = 0,      ///< nothing to report
        Succeeded = 1, ///< an artifact was written
        Failed = 2     ///< the agent (or this client) refused
    };

    /// @brief QML-instantiation ctor: co-owns the process-wide
    ///        `sharedAgentClient()`, so every widget (and the config dialog)
    ///        reuses ONE agent connection + ObjectManager discovery.
    explicit SmartCardHandler(QObject* parent = nullptr);
    /// @brief Inject a client (tests pass one driven by a fake agent peer).
    ///        Co-owned: several handlers may share one client, mirroring the
    ///        production sharedAgentClient() shape.
    explicit SmartCardHandler(std::shared_ptr<LibreSCRS::AgentClient::AgentClient> client, QObject* parent = nullptr);
    ~SmartCardHandler() override;

    /// QML-facing — returns `CardStateModel::State` cast to int (the QML
    /// `Loader` switches on this raw int).
    [[nodiscard]] int state() const noexcept;
    [[nodiscard]] QString readerName() const
    {
        return m_readerName;
    }
    [[nodiscard]] QString errorMessage() const
    {
        return m_errorMessage;
    }
    [[nodiscard]] QString cardLabel() const
    {
        return m_cardLabel;
    }
    [[nodiscard]] bool hasCardPhoto() const
    {
        return m_hasCardPhoto;
    }
    [[nodiscard]] QString cardPhotoUrl() const
    {
        return m_cardPhotoUrl;
    }
    [[nodiscard]] QString photoSuggestedFileName() const
    {
        return m_photoSuggestedFileName;
    }
    [[nodiscard]] bool agentInstalled() const
    {
        return m_agentInstalled;
    }
    [[nodiscard]] bool cardDetected() const
    {
        return m_cardDetected;
    }
    [[nodiscard]] bool busy() const
    {
        return m_busy;
    }
    [[nodiscard]] bool libreCelikAvailable() const
    {
        return !m_libreCelikPath.isEmpty();
    }
    /// @brief Whether the active card advertises PIN management (`Cap::PinManagement`).
    ///        Defined out-of-line: reads the live card's capabilities.
    [[nodiscard]] bool pinManagementAvailable() const;
    [[nodiscard]] bool hasIdentity() const
    {
        return !m_identityFields.isEmpty();
    }
    [[nodiscard]] QVariantList identityFields() const
    {
        return m_identityFields;
    }
    [[nodiscard]] QVariantList identitySummary() const
    {
        return m_identitySummary;
    }
    [[nodiscard]] QVariantList identityDetails() const
    {
        return m_identityDetails;
    }
    /// @brief Whether the DISPLAYED reader's card has a sign in flight.
    ///        Defined out-of-line: reads the per-reader sign store.
    [[nodiscard]] bool signingBusy() const;
    /// @brief The DISPLAYED reader's sign phase (OperationPhase int), or
    ///        Created(0) when it is not signing.
    [[nodiscard]] int signPhase() const;
    /// @brief The DISPLAYED reader's last sign outcome (see the `signResult`
    ///        property). Never null: a reader with nothing to report answers
    ///        with `outcome` = None.
    [[nodiscard]] QVariantMap signResult() const;
    /// @brief Dismiss the DISPLAYED reader's sign result (the banner's close
    ///        button). Reader-scoped: another card's result is untouched.
    Q_INVOKABLE void dismissSignResult();
    [[nodiscard]] int operationPhase() const
    {
        return m_operationPhase;
    }
    /// @brief Localized, user-facing label for an OperationPhase int (ki18nc,
    ///        librekde domain — mirrors ErrorText). Unknown/none phases and
    ///        Created/Connecting/Done map to a generic "Working…".
    Q_INVOKABLE [[nodiscard]] QString operationPhaseLabel(int phase) const;
    [[nodiscard]] QString boundReaderName() const
    {
        return m_boundReaderName;
    }
    [[nodiscard]] bool boundReaderPresent() const
    {
        return m_boundReaderPresent;
    }
    [[nodiscard]] QStringList availableReaderNames() const
    {
        return m_availableReaderNames;
    }
    [[nodiscard]] QStringList readersWithCards() const
    {
        return m_readersWithCards;
    }
    /// @brief Set the persisted per-widget binding. Empty = Auto. Re-resolves the
    ///        active card and clears any Auto-mode transient pick.
    void setBoundReaderName(const QString& name);

    [[nodiscard]] bool viewActive() const
    {
        return m_viewActive;
    }
    /// @brief Flip content visibility (bound to the popup's `expanded` in
    ///        main.qml). Turning it ON re-checks the free read.
    void setViewActive(bool active);

    /// Pure, testable install probe: true if any @p dataDirs entry carries the
    /// agent's D-Bus activation file or its systemd user unit.
    [[nodiscard]] static bool agentServiceInstalledIn(const QStringList& dataDirs);

    /// Pure, testable: the card:/ URL for a reader friendly name.
    [[nodiscard]] static QUrl cardUrlForReader(const QString& readerName);

    /// @brief Launch the standalone credential-management window for the
    ///        active card's reader, so the user can change / unblock its PIN.
    ///        No-op when no card is bound. Thin client: the window owns its OWN
    ///        agent session — this handler issues no card I/O and holds no secret.
    ///        Shown by QML only when `pinManagementAvailable`.
    Q_INVOKABLE void manageCredentials();

    /// Pure, testable seam behind `manageCredentials()`: the exact argv the
    /// standalone credentials window's `--reader` option expects for the opaque
    /// reader id @p readerId — `{"--reader", readerId}`. The id is forwarded
    /// verbatim; nothing here parses it.
    [[nodiscard]] static QStringList credentialsLaunchArgs(const QString& readerId);

    /// Pure, testable: friendly display labels for raw PC/SC reader names,
    /// index-aligned with @p rawNames. Strips the pcsc-lite boilerplate
    /// (`(serial) <ifd> <slot>`), shortens to the model (preferring the
    /// bracketed product string), and — when a dual-interface reader surfaces
    /// both — disambiguates with "— contact" / "— contactless". Never empty
    /// (falls back to the raw name) and never collides (a short serial tail is
    /// appended if two labels would otherwise match). The RAW name stays the
    /// binding/selection key everywhere; this only affects what is SHOWN.
    [[nodiscard]] static QStringList readerDisplayLabels(const QStringList& rawNames);

    /// The stable identity of a PC/SC reader name for bound-reader matching: the
    /// parenthesized unit serial (e.g. `69988A87` from "Gemalto PC Twin Reader
    /// (69988A87) 00 00"; `IM0O2C00NF10456904` from the OMNIKEY names), or empty
    /// when the name carries none, only a short token, or a digitless token
    /// (driver databases ship model-static "(CCID)"/"(ICCD)"/"(Liteon)" suffixes
    /// identical for every unit of those models). The serial is stable across
    /// the volatile trailing " NN NN" enumeration index that shifts with plug
    /// order. Pure; no D-Bus.
    [[nodiscard]] static QString readerSerialKey(const QString& name);

    /// The model part of a PC/SC reader name: the name minus its last
    /// parenthesized group (the serial-key candidate) and the trailing " NN NN"
    /// enumeration indices. Two names may be equated by a shared serial key only
    /// when their bases agree (see `sameReaderUnit`). Pure; no D-Bus.
    [[nodiscard]] static QString readerBaseName(const QString& name);

    /// Whether two PC/SC reader names denote the SAME physical unit across a
    /// re-enumeration: both carry a qualifying unit serial, the serials agree,
    /// and the base names (model part) agree — so a generic or shared token
    /// never equates different models, and the contact/contactless entries of a
    /// dual-interface reader (one USB serial, different bracketed model
    /// strings) never equate each other. Pure; no D-Bus.
    [[nodiscard]] static bool sameReaderUnit(const QString& a, const QString& b);

    /// The friendly label for a single raw reader name, resolved against the
    /// current roster (so contact/contactless disambiguation is stable). Falls
    /// back to @p rawName for a reader not in the roster (e.g. a remembered but
    /// absent bound reader). QML display seam; selection/binding still use the
    /// raw name.
    Q_INVOKABLE [[nodiscard]] QString readerDisplayName(const QString& rawName) const;

    /// Pure, testable: neutralize rich-text promotion for card/agent/filename-
    /// derived values rendered by sinks whose `textFormat` cannot be forced to
    /// `Text.PlainText` (Kirigami `InlineMessage`/`PlaceholderMessage` internals
    /// and QQC2/PC3 button labels — all verified `AutoText`, where
    /// `Qt::mightBeRichText` promotes tag-looking values to StyledText). A value
    /// Qt would promote is HTML-escaped; the same AutoText path then renders the
    /// escaped form as the ORIGINAL literal characters (the entity sequences are
    /// themselves rich-detected — verified empirically). Values without markup
    /// pass through byte-identical, honouring the raw-card-data rule for every
    /// legitimate string. Controllable sinks use `textFormat: Text.PlainText`
    /// directly instead.
    Q_INVOKABLE [[nodiscard]] static QString plainDisplay(const QString& text);

    /// Pure, testable: the suggested save-file name for a photo's RAW bytes,
    /// extension sniffed from the actual image format ("card-photo.jpg" for
    /// JPEG, ".png" for PNG, ".jp2" for JPEG2000, bare "card-photo" when the
    /// format cannot be determined).
    [[nodiscard]] static QString suggestedPhotoFileName(const QByteArray& bytes);

    /// Pure, testable: curate the identifying summary rows from a flattened
    /// identity model (each entry a QVariantMap { groupKey, fieldKey, label,
    /// value }). Picks curated identifying keys in a fixed preferred order;
    /// falls back to the first rows so the result is NEVER empty.
    ///
    /// DG1-primary rule: the machine-verified MRZ name components (surname /
    /// family_name / given_name / given_names) lead the summary; a
    /// supplementary full_name row (eMRTD DG11 — free-form, on some
    /// documents not a person name at all) becomes the headline ONLY when no
    /// non-empty MRZ name component is present in @p fields.
    [[nodiscard]] static QVariantList curateIdentitySummary(const QVariantList& fields);

    /// Pure, testable: the rows of @p fields that @p summary does NOT already
    /// show, in their original order — the other half of the split.
    ///
    /// `curateIdentitySummary` + this function PARTITION @p fields: together
    /// they contain every row exactly once. Rows are matched by their
    /// (groupKey, fieldKey) identity, not by their rendered text, so a key
    /// that legitimately appears under TWO groups keeps the copy the summary
    /// did not take.
    [[nodiscard]] static QVariantList curateIdentityDetails(const QVariantList& fields, const QVariantList& summary);

    /// @brief The shared photo store the QML `CardPhotoProvider` reads from.
    ///        Co-owned (shared_ptr) so the provider can outlive this handler.
    [[nodiscard]] std::shared_ptr<CardPhotoStore> photoStore() const
    {
        return m_photoStore;
    }

    /// @brief This handler's slot in the shared `CardPhotoStore`. Process-unique
    ///        per instance, so two widgets showing two cards never alias each
    ///        other's photo; baked into `cardPhotoUrl` for the provider lookup.
    [[nodiscard]] quint64 photoSlot() const
    {
        return m_photoSlot;
    }

public Q_SLOTS:
    /// Launch LibreCelik as a sibling process for the full card UI.
    void openInLibreCelik();

    /// Start an agent ReadIdentity on the active card. For a pre-read-locked
    /// card the agent raises its own secure prompter to collect the CAN/MRZ;
    /// the client never sees the secret. Bound to the PreAuthState /
    /// IdentityState affordances in QML. No-op when no card is active.
    void readIdentity();

    /// The "free read" — issued on FIRST VIEW, not at insertion.
    /// Driven internally off `viewActive` (popup opens) and off every active-
    /// card (re)classification while the view is active — card landing, chip
    /// selection, same-reader swap — so an open popup never shows a blank
    /// identity body even when the switch fires no stateChanged.
    /// Reads only a no-secret card (PreReadAuth::None, identity-capable)
    /// that has not been read yet; a Can/Mrz card is NEVER read here
    /// (the lazy card-I/O invariant — no surprise CAN/MRZ prompt), and repeat
    /// calls are no-ops. Keeps identity/photo PII out of plasmashell memory
    /// for widgets nobody opens. Public: also a direct test seam.
    void ensureFreeRead();

    /// Sign a local file with the active card's signing certificate, writing the
    /// artifact next to the input. Shares the exact
    /// `SignJob` core + dialog seams as the Purpose plugin — never a second
    /// signing path. @p fileUrl is the QML `FileDialog` selection (a `file://`
    /// URL or a plain path). No-op while a sign is already in flight; the agent
    /// raises its own PIN prompter. Gated on `Cap::Pki` by the QML that shows it.
    void signFile(const QString& fileUrl);

    /// Ask systemd --user to start the D-Bus-activatable agent. When it registers
    /// on the bus, availabilityChanged(true) drives refresh() and the state
    /// leaves AgentUnavailable. Best-effort; no-op if systemctl is unavailable.
    void startAgent();

    /// User-initiated re-check behind the plasmoid's refresh affordance. Re-runs
    /// client discovery (`AgentClient::refreshDiscovery` → re-probe bus name +
    /// GetManagedObjects; reuses the existing wire, no new D-Bus method) and
    /// re-detects whether the agent is installed. Recovers from a dropped
    /// `InterfacesAdded` (a card the agent exported but the client never saw) or
    /// a missed `NameOwnerChanged`; a no-op-safe manual nudge otherwise, since
    /// discovery already reconciles the registry. `refreshDiscovery` emits
    /// `readersChanged`, which drives `refresh()`.
    void requestRefresh();

    /// Copy a rendered field value to the clipboard.
    void copyField(const QString& value);
    /// Save the active card's face photo (RAW agent bytes, original format) to
    /// @p destination. Returns false if there is no photo or the write failed.
    bool savePhoto(const QUrl& destination);
    /// Open the active reader's card in the default file manager (card:/<reader>).
    void openInFiles();

    /// Auto-mode master-detail pick: make @p friendlyName the active card among
    /// several present. No-op in bound mode (the widget is pinned) or when the
    /// name is already selected. Bound to the reader chips in MultiCardState.qml.
    void selectReader(const QString& friendlyName);

Q_SIGNALS:
    void stateChanged();
    void pinManagementAvailableChanged();
    void readerNameChanged();
    void errorMessageChanged();
    void cardLabelChanged();
    void cardPhotoChanged();
    void agentInstalledChanged();
    void cardDetectedChanged();
    void identityChanged();
    void busyChanged();
    void signingBusyChanged();
    void signPhaseChanged();
    void signResultChanged();
    void operationPhaseChanged();
    void boundReaderNameChanged();
    void boundReaderPresentChanged();
    void availableReaderNamesChanged();
    void readersWithCardsChanged();
    void viewActiveChanged();

private:
    /// One reader's sign state. Keyed in `m_signStates` by the RAW PC/SC reader
    /// name — the same key `boundReaderName`, `selectReader()` and
    /// `readersWithCards` already bind on. The card the sign was started on is
    /// held too, so a swapped card never inherits the previous card's spinner:
    /// an entry only speaks for the card it belongs to.
    struct ReaderSignState
    {
        QPointer<LibreKDE::SignJob> job;                  ///< live sign, else null
        QPointer<LibreSCRS::AgentClient::AgentCard> card; ///< the card it runs on
        int phase = 0;                                    ///< OperationPhase::Created
        QString certLabel;                                ///< implicit multi-cert pick
        SignOutcome outcome = SignOutcome::None;          ///< last terminal, until dismissed
        QString outputPath;                               ///< artifact written (Succeeded)
        QString level;                                    ///< AdES level the agent reported
        QString message;                                  ///< localized diagnostic (Failed)
    };

    void wireClient();
    /// Re-pick the active reader/card from the client and re-classify.
    void refresh();
    /// Recompute availableReaderNames + readersWithCards from the live registry;
    /// emit their changed() signals only on an actual change.
    void updateReaderRosters();
    /// Recompute boundReaderPresent from the bound name + live roster; fire its
    /// change signal only on a flip. Called unconditionally from
    /// updateReaderRosters (which runs on every refresh, including after a
    /// rebind), so both a roster change and a binding change update it.
    void updateBoundReaderPresent();
    /// Resolve the reader whose card the widget should reflect, honouring the
    /// bound-reader pin and (in Auto) the transient master-detail selection.
    LibreSCRS::AgentClient::AgentReader* pickActiveReader();
    void bindCard(LibreSCRS::AgentClient::AgentCard* card);
    void classifyActiveCard();
    /// The identity read reached its terminal. The operation carries the whole
    /// outcome (status / error code / call classification / agent message), so
    /// this reads them off `m_identityOp` rather than taking them as arguments.
    void onOperationFinished();

    /// Best-effort: after a successful identity read, drive `AgentCard::getPhoto`
    /// async and decode the first sealed-memfd photo into the store. A missing or
    /// failed photo is NOT surfaced as an error — it just leaves `hasCardPhoto`
    /// false. Signal-driven; no nested event loop on the GUI thread.
    void startPhotoRead();
    void onPhotoFinished();
    void clearPhoto();

    /// Fire-and-forget: warm the agent's shared certificate read cache so the
    /// `card:/` file-manager view's PKI folder is instant on first open. The
    /// plasmoid reads identity, not certs; this pre-reads them (reusing the warm
    /// PACE session) and the agent dedups it against the KIO worker's later read,
    /// so it is ONE shared card read. Genuinely asynchronous — the entry call
    /// never blocks the GUI thread (see AgentCard::warmCertificates). No busy
    /// state, no UI, best-effort; a card without PKI certs refuses the method at
    /// entry and nothing is reported either way. The client debounces a warm
    /// issued while one is still in flight for the same card, so this needs no
    /// re-issue guard of its own.
    void warmCertificateCache();
    void rebuildIdentityModel(const QList<LibreSCRS::AgentClient::FieldGroup>& groups);
    void clearIdentity();

    void setReaderName(const QString& name);
    void setCardLabel(const QString& label);
    void transitionTo(CardStateModel::State newState);
    /// Recompute the cached pinManagementAvailable flag from the bound card's
    /// LIVE capabilities and fire its change signal when it flips. Called on
    /// every (re)classification and on the card-less refresh branches, so a
    /// capability flip that keeps the coarse state still updates the launcher.
    void updatePinManagementAvailable();
    void setError(const QString& diagnosticOrEmpty);
    [[nodiscard]] static bool detectAgentInstalled();
    void setAgentInstalled(bool installed);
    void setCardDetected(bool detected);
    /// Scope-aware: in bound mode, whether the reader `pickActiveReader` would
    /// choose reports a card — the bound-name entry while it is listed, else any
    /// same-unit re-enumeration candidate; in Auto mode, whether ANY reader
    /// reports a card. Called only from the card-less (NoCard) refresh branch,
    /// where a present card necessarily means the agent has not yet exported a
    /// resolvable `Card1` for it (else `pickActiveReader` would have returned a
    /// card).
    [[nodiscard]] bool computeCardDetected() const;
    [[nodiscard]] static QString locateLibreCelik();

    void setBusy(bool busy);
    /// Store + notify the current identity-read phase (no-op if unchanged).
    void setOperationPhase(LibreSCRS::AgentClient::OperationPhase phase);

    /// The displayed reader's sign entry, or nullptr when it has none — or when
    /// the entry belongs to a DIFFERENT card than the one bound now (a swapped
    /// card must not inherit the previous card's sign surfaces).
    [[nodiscard]] const ReaderSignState* activeSignState() const;
    /// Recompute the reader-scoped sign surfaces and emit only what actually
    /// changed. Called from every site that can move them: a sign starting,
    /// its phase advancing, its terminal, and every re-target (`refresh()`),
    /// since the displayed reader itself is an input.
    void publishSignSurfaces();

    /// @brief Allocate a process-unique photo slot (monotonic counter).
    [[nodiscard]] static quint64 nextPhotoSlot();

    // Co-owned, process-shared agent client (sharedAgentClient() in production;
    // tests inject one driven by a fake agent peer, possibly shared across
    // handlers).
    std::shared_ptr<LibreSCRS::AgentClient::AgentClient> m_client;
    QPointer<LibreSCRS::AgentClient::AgentCard> m_card;
    QPointer<LibreSCRS::AgentClient::AgentOperation> m_identityOp;
    QPointer<LibreSCRS::AgentClient::AgentOperation> m_photoOp;
    /// Live sign state per reader. Entries are minted by `signFile()` and pruned
    /// by `updateReaderRosters()` once their reader leaves the roster and no job
    /// is left running on it.
    QHash<QString, ReaderSignState> m_signStates;
    /// Last published values of the reader-scoped sign surfaces, so
    /// `publishSignSurfaces()` fires each change signal only on a real flip.
    bool m_publishedSigningBusy = false;
    int m_publishedSignPhase = 0;
    QVariantMap m_publishedSignResult;
    int m_operationPhase = 0; // OperationPhase::Created (the identity READ's phase)
    bool m_identityRead = false;
    /// Cached "Manage credentials…" gate (see updatePinManagementAvailable()).
    bool m_pinManagementAvailable = false;

    // The process-wide shared store (Q_GLOBAL_STATIC), NOT a per-handler store:
    // the QML CardPhotoProvider binds to the same store without resolving any
    // handler at engine init (see sharedCardPhotoStore() in CardPhotoStore.h).
    // Each handler writes only its OWN slot (m_photoSlot), so per-widget photos
    // stay isolated across plasmoid instances in the shared engine.
    std::shared_ptr<CardPhotoStore> m_photoStore = sharedCardPhotoStore();
    const quint64 m_photoSlot = nextPhotoSlot();
    bool m_hasCardPhoto = false;
    QString m_cardPhotoUrl;
    QString m_photoSuggestedFileName;
    quint64 m_photoToken = 0;
    bool m_agentInstalled = false;
    bool m_cardDetected = false; // NoCard branch: a card is seated but not yet resolved
    bool m_busy = false;
    QByteArray m_photoBytes;                             ///< Raw agent photo bytes (retained for Save photo…).
    const QString m_libreCelikPath = locateLibreCelik(); ///< Detected once at construction.

    CardStateModel::State m_state = CardStateModel::State::NoCard;
    QString m_readerName;
    QString m_errorMessage;
    QString m_cardLabel;
    QVariantList m_identityFields;
    QVariantList m_identitySummary;
    QVariantList m_identityDetails;
    bool m_viewActive = false;                    // content visible (popup expanded); gates the free read
    QString m_boundReaderName;                    // persisted per-widget binding ("" = Auto)
    bool m_boundReaderPresent = false;            // bound reader currently on the bus (in the roster)
    QString m_selectedReaderName;                 // Auto-mode transient master-detail pick
    QStringList m_availableReaderNames;           // config chooser source (all readers)
    QStringList m_readersWithCards;               // master list (readers holding a card)
    QHash<QString, QString> m_readerDisplayNames; // raw reader name -> friendly label (roster-derived)
};

} // namespace LibreKDE::Plasmoid
