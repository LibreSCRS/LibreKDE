// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CredentialModel.h"

#include <LibreSCRS/AgentClient/CredentialTypes.h> // CredentialKind — the presented-credential attribution
#include <LibreSCRS/AgentClient/SyncError.h>       // the named refusal a verb can come back with

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

namespace LibreSCRS::AgentClient {
class AgentClient;
class AgentCard;
class AgentOperation;
} // namespace LibreSCRS::AgentClient

namespace LibreKDE::Credentials {

/// @brief QML-facing controller for the standalone credential-management window
///        (`librescrs-credentials-kde`).
///
/// Registered as an instantiable QML type via
/// `qt_add_qml_module(URI org.librescrs.credentials)`; the window's `Main.qml`
/// creates ONE `CredentialController { }` and switches its placeholder body on
/// `controller.state`. A thin client of `org.librescrs.Agent`: NO card session,
/// NO secrets — card I/O and PIN/PUK/CAN entry live in the agent, whose secure
/// prompter collects secrets when an operation needs them. The controller only
/// reflects reader/card state and drives the credential list + PIN/signing-key
/// verbs.
///
/// Shape mirrors `SmartCardHandler`: a co-owned, process-shared `AgentClient`
/// (`sharedAgentClient()` in production, injected for tests), a `QPointer` to
/// the bound `AgentCard`, and `availabilityChanged`/`cardChanged` wiring.
///
/// State machine (classification; the list flow + verbs reach the
/// `Ready`/`Empty`/`Working`/`Result` states):
///   - agent not reachable                         -> AgentUnavailable
///   - no card in the bound reader                 -> NoCard
///   - card present, no `Cap::PinManagement` bit   -> NotManageable
///   - card present AND manageable                 -> Loading (kicks off
///                                                    `listCredentials`)
///
/// Single-threaded: every `AgentClient` signal arrives on the QML thread
/// (QtDBus dispatch).
class CredentialController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    /// Current `State` cast to int — the QML body switches on this.
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    /// Friendly Name of the bound reader (empty when none is bound / resolvable).
    Q_PROPERTY(QString readerName READ readerName NOTIFY readerNameChanged)
    /// The credential list the dashboard's `ListView` binds to. Created once with
    /// the controller and populated on each `ListCredentials` result, so the
    /// pointer is CONSTANT — only its rows change (via model reset).
    Q_PROPERTY(LibreKDE::Credentials::CredentialModel* credentials READ credentials CONSTANT)
    /// The localized banner text for the last finished verb (empty = show nothing,
    /// e.g. a neutral user-cancel). The `ResultBanner` binds its `text` here and is
    /// visible only in the `Result` state.
    Q_PROPERTY(QString resultMessage READ resultMessage NOTIFY resultChanged)
    /// Whether the last verb's result is an error (a red banner) versus a neutral
    /// or success notice (an informational banner). Never true for a user cancel.
    Q_PROPERTY(bool resultIsError READ resultIsError NOTIFY resultChanged)
    /// Best-effort progress [0,1] of the in-flight verb, driven off the agent's
    /// Operation `Phase`/`Progress`; 0 until the agent reports any. The `Working`
    /// affordance may show a determinate bar, else an indeterminate spinner.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    /// The truthful outcome of a mutation terminated by a card pull ("" = none).
    /// A card removed mid-verb lands the window in `NoCard`, where the `Result`
    /// banner never renders — so the notice rides the NoCard placeholder's
    /// explanation instead. Set by the card-removed mutation terminal; cleared
    /// when a card (re)binds or a fresh verb starts.
    Q_PROPERTY(QString removalNotice READ removalNotice NOTIFY removalNoticeChanged)
public:
    /// Coarse UI states the window renders. The value is carried on the `state`
    /// property; `Q_ENUM` registers the enumerators for QML, which addresses them
    /// unscoped (`CredentialController.NoCard`) exactly as the plasmoid's
    /// `CardStateModel::State` is consumed. The ORDER is wire-stable — QML
    /// switches on the enumerators — so do not renumber.
    enum class State : int {
        AgentUnavailable = 0, ///< The agent is not reachable on the session bus.
        NoCard,               ///< No card present in the bound reader.
        NotManageable,        ///< Card present but without the PinManagement capability.
        Loading,              ///< Card present + manageable; the credential list is being fetched.
        Ready,                ///< Credentials fetched; the dashboard is populated.
        Empty,                ///< Card manageable but exposes no credentials.
        Working,              ///< A PIN/signing-key verb is in flight.
        Result,               ///< A verb finished; the result banner is shown.
        // Appended (NOT inserted) so the existing enumerators keep their wire-stable
        // values — the QML switches by NAME, but the ORDER discipline is preserved.
        ReadFailed, ///< Manageable card whose credential list could not be
                    ///< read (a non-Ok/cancelled read) — NOT an unsupported card.
    };
    Q_ENUM(State)

    /// @brief QML-instantiation ctor: co-owns the process-wide
    ///        `sharedAgentClient()`.
    explicit CredentialController(QObject* parent = nullptr);
    /// @brief Inject a client (tests pass one pointed at a `FakeAgent` peer).
    ///        Co-owned, mirroring the production shared-client shape.
    explicit CredentialController(std::shared_ptr<LibreSCRS::AgentClient::AgentClient> client,
                                  QObject* parent = nullptr);
    ~CredentialController() override;

    CredentialController(const CredentialController&) = delete;
    CredentialController& operator=(const CredentialController&) = delete;

    [[nodiscard]] int state() const noexcept
    {
        return static_cast<int>(m_state);
    }
    [[nodiscard]] QString readerName() const
    {
        return m_readerName;
    }
    [[nodiscard]] CredentialModel* credentials() const noexcept
    {
        return m_model;
    }
    [[nodiscard]] QString resultMessage() const
    {
        return m_resultMessage;
    }
    [[nodiscard]] bool resultIsError() const noexcept
    {
        return m_resultIsError;
    }
    [[nodiscard]] double progress() const noexcept
    {
        return m_progress;
    }
    [[nodiscard]] QString removalNotice() const
    {
        return m_removalNotice;
    }

    /// @brief Bind (or re-target) to a reader by its opaque agent-side id (the
    ///        `--reader` argument the plasmoid passes). Re-classifies the bound
    ///        reader's card; a re-bind of the same id still refreshes (a
    ///        second launch may re-target after the card was swapped).
    Q_INVOKABLE void bindReader(const QString& readerId);
    /// @brief Re-resolve the bound reader's card from the client and re-classify.
    ///        A missing/stale binding falls back to `firstReaderWithCard()`
    ///        before resting on NoCard; the explicit binding stays
    ///        sticky and wins again once it resolves.
    Q_INVOKABLE void refresh();
    /// @brief Cancel the in-flight credential-list fetch or the running PIN
    ///        verb. No-op when nothing is in flight.
    Q_INVOKABLE void cancel();

    /// @name Verb invokables.
    /// The delegate's action buttons bind to these. Each mints the matching agent
    /// mutation (secret-free here — the agent's secure prompter collects the PIN/
    /// PUK/CAN), drives the window through `Working`, renders the typed `PinResult`
    /// in `Result`, then runs the MANDATORY post-mutation re-list.
    /// @{
    /// Change a PIN (`managePin(id, PinVerb::Change)`).
    Q_INVOKABLE void changePin(const QString& id);
    /// Pre-flight for unblocking @p id: format the PUK's remaining budget from the
    /// current model and emit `unblockConfirmRequested(id, budgetText)`. The actual
    /// PUK entry happens in the agent's prompter after `confirmUnblock`.
    Q_INVOKABLE void requestUnblock(const QString& id);
    /// User confirmed the unblock sheet (`managePin(id, PinVerb::Unblock)`).
    Q_INVOKABLE void confirmUnblock(const QString& id);
    /// Activate a transport PIN (`managePin(id, PinVerb::ActivatePin, {activateKey})`),
    /// bringing up the on-card signing key in the same flow when it is pending.
    Q_INVOKABLE void activate(const QString& id);
    /// Activate the on-card signing key on its own (`activateSigningKey()`), the
    /// standalone affordance the re-list advertises after a partial bring-up.
    Q_INVOKABLE void activateSigningKey(const QString& id);
    /// @}

    /// Pure, testable: neutralize rich-text promotion for agent/hardware-derived
    /// values (reader names, agent guidance fallbacks, budget lines) rendered by
    /// sinks whose `textFormat` cannot be forced to `Text.PlainText` (Kirigami
    /// `PlaceholderMessage`/`InlineMessage` internals and `PromptDialog`
    /// subtitles — all `AutoText`, where `Qt::mightBeRichText` promotes
    /// tag-looking values to StyledText). A value Qt would promote is
    /// HTML-escaped; the same AutoText path then renders the escaped form as the
    /// ORIGINAL literal characters (the entity sequences are themselves
    /// rich-detected). Values without markup pass through byte-identical, so no
    /// legitimate string is ever altered. Controllable sinks use
    /// `textFormat: Text.PlainText` directly instead. Mirrors the plasmoid's
    /// `SmartCardHandler::plainDisplay`.
    Q_INVOKABLE [[nodiscard]] static QString plainDisplay(const QString& text);

Q_SIGNALS:
    void stateChanged();
    void readerNameChanged();
    void resultChanged();
    void progressChanged();
    void removalNoticeChanged();
    /// @brief Ask the QML to raise the unblock confirm sheet for credential @p id,
    ///        showing @p pukBudgetText (the PUK's remaining unblock budget). The
    ///        sheet's Continue calls `confirmUnblock(id)`; Cancel just dismisses.
    void unblockConfirmRequested(const QString& id, const QString& pukBudgetText);

private:
    void wireClient();
    /// Track the bound card + its capability-change signal (re-classify on change).
    void bindCard(LibreSCRS::AgentClient::AgentCard* card);
    /// Classify the currently bound card into a State.
    void classify();
    /// Kick a `ListCredentials` on the bound card (unless already in flight or
    /// settled) and drive the model + Loading → Ready/Empty/ReadFailed off its
    /// terminal `finished`.
    void startListCredentials();
    /// A `ListCredentials` operation reached its terminal status. Drives the whole
    /// outcome off `finished` alone (every polled value and the typed result are
    /// settled before it fires, so reading them here is race-free): Ok →
    /// Ready/Empty; non-Ok → ReadFailed. Clears `m_listOp` on EVERY path; latches
    /// `m_listSettled` on success and on an explicit user cancel, but NOT on a
    /// transient error (which stays re-fetchable on the next card/availability
    /// event or an explicit refresh()).
    void onListFinished(LibreSCRS::AgentClient::AgentOperation* op);
    /// Tear down our interest in the in-flight list op (disconnect + reap) and
    /// clear the settled latch (and the result-hold), so the next classify
    /// re-fetches. Does not change state — the caller sets it.
    void resetListState();
    /// Detach (disconnect + agent-side cancel) the in-flight mutation op and
    /// clear its pending state. Called when the card binding changes: the op
    /// belongs to the PREVIOUS binding, so its terminal must not fire the old
    /// card's banner / mandatory re-list over the new target. No-op when no
    /// mutation is in flight.
    void detachMutation();
    void transitionTo(State next);
    void setReaderName(const QString& name);

    /// Adopt a freshly minted `ManagePin` / `ActivateSigningKey` mutation: enter
    /// `Working` and wire `finished`/`phaseChanged`. Every minted operation is
    /// non-null — a call the agent refuses at method entry comes back as an
    /// operation that terminalizes immediately with the refusal, so a refusal is
    /// reached through `onMutationDone` like any other outcome, never here.
    /// @p presented is stashed for the result's attribution.
    void beginMutation(LibreSCRS::AgentClient::AgentOperation* op, LibreSCRS::AgentClient::CredentialKind presented);
    /// A mutation reached its terminal `finished`. A refusal the agent NAMED
    /// (`syncError()` engaged) never reached the card and is handed to
    /// `handleRefusal`; everything else is driven off `op->pinResult()` alone (NOT
    /// the terminal status — a soft-fail invalidPin/blocked legitimately finishes
    /// Error yet carries a real outcome): set the attributed result banner, enter
    /// `Result`, then run the MANDATORY re-list.
    void onMutationDone(LibreSCRS::AgentClient::AgentOperation* op);
    /// A `ManagePin`/`ActivateSigningKey` the agent refused at method entry, by
    /// name. Branch on that name: `UnknownCredential` → neutral notice + recovery
    /// re-list (the agent dropped its cache — a stale id); `RateLimited` → neutral
    /// "please wait" (the request never reached the card, the listing is still
    /// valid, no re-list); `InvalidRequest` → a USER-REACHABLE refusal (the agent
    /// maps an ambiguous-credential card condition onto it) — neutral notice, no
    /// re-list (persistent for the card); any other name → a neutral generic
    /// notice + a defensive re-list. Named refusals are NEVER red banners.
    void handleRefusal(LibreSCRS::AgentClient::SyncError named, LibreSCRS::AgentClient::AgentOperation* op);
    /// The MANDATORY post-mutation re-list: the agent invalidates its
    /// listing cache on any mutation that reached the card, so re-fetch before
    /// offering another action. Keeps the `Result` banner up while the fresh list
    /// loads (no `Loading` flash), transitioning to `Ready`/`Empty` only once new
    /// rows land. The agent's secret cache is untouched, so the re-list reuses the
    /// cached CAN — it never re-prompts.
    void relistAfterMutation();
    /// The credential kind PRESENTED in @p id's failing step, for the result
    /// attribution: the record's own kind (or `Unknown` when the id is stale).
    [[nodiscard]] LibreSCRS::AgentClient::CredentialKind kindOf(const QString& id) const;
    /// A verb may start only on a bound (manageable) card with no verb already in
    /// flight — the public invokables guard on this (the Working scrim is UI-only).
    [[nodiscard]] bool canStartMutation() const;
    void setResult(bool isError, const QString& message);
    void setProgress(double progress);
    void setRemovalNotice(const QString& notice);

    // Co-owned, process-shared agent client (sharedAgentClient() in production;
    // tests inject one pointed at a fake peer).
    std::shared_ptr<LibreSCRS::AgentClient::AgentClient> m_client;
    QPointer<LibreSCRS::AgentClient::AgentCard> m_card;
    /// The in-flight (or last) ListCredentials op, parented to the bound card.
    QPointer<LibreSCRS::AgentClient::AgentOperation> m_listOp;
    /// The in-flight PIN/signing-key mutation op, parented to the bound card.
    QPointer<LibreSCRS::AgentClient::AgentOperation> m_mutationOp;
    /// The dashboard's model; owned by (parented to) this controller.
    CredentialModel* m_model = nullptr;
    QString m_readerId;   ///< Bound reader id ("" = none bound).
    QString m_readerName; ///< Friendly Name of the bound reader.
    /// Id of the reader the binding last RESOLVED to (explicit target or
    /// fallback; "" = none). Lets bindReader() recognize an explicit re-bind
    /// of the reader already being managed, which must never abandon a running
    /// verb the way a genuine re-target does.
    QString m_resolvedReaderId;
    QString m_resultMessage;      ///< Last verb's banner text ("" = none).
    bool m_resultIsError = false; ///< Last verb's result is an error banner.
    double m_progress = 0.0;      ///< In-flight verb progress [0,1].
    /// Card-removed outcome carried on the NoCard surface (see the property).
    QString m_removalNotice;
    /// Kind PRESENTED by the in-flight mutation, stashed at invoke time for the
    /// result's attribution: Change/ActivatePin → the record's kind,
    /// Unblock → Puk, activateSigningKey → Sign.
    LibreSCRS::AgentClient::CredentialKind m_pendingPresentedKind = LibreSCRS::AgentClient::CredentialKind::Unknown;
    /// True while a finished verb's `Result` banner must stay up across the
    /// mandatory re-list: suppresses the re-list's `Loading` transition so the
    /// outcome remains visible until fresh rows land (then onListFinished clears it
    /// and moves to Ready/Empty). Cleared by resetListState and onListFinished.
    bool m_showingResult = false;
    /// True once the user cancelled the in-flight mutation (scrim Cancel). The
    /// authoritative "this was a cancel" signal: an aborted credentials op whose
    /// agent delivered no `userCancelled` Result finishes Error/CommunicationError
    /// with `pinResult` left at Unspecified (finalizeTerminal rewrites the status),
    /// so neither the terminal status nor the outcome alone is reliable — but the
    /// user's own request is. onMutationDone renders any cancel NEUTRAL, never red.
    /// Set by cancel(), reset at the start of each mutation and once consumed.
    bool m_mutationCancelRequested = false;
    /// True once the user cancelled the in-flight LIST fetch. The mutation flag's
    /// mirror (the list-cancel latch must not depend on a preserved Cancelled
    /// terminal): a cancelled read whose abort delivered no Result arrives at
    /// onListFinished rewritten to Error/CommunicationError, and only this flag
    /// still knows it was the user's own choice — which must latch
    /// (`m_listSettled`), never auto-re-prompt. Set by cancel(), consumed by
    /// onListFinished, cleared by resetListState.
    bool m_listCancelRequested = false;
    State m_state = State::NoCard;
    /// True once the bound card's list is "settled" and must NOT be auto-re-fetched
    /// on an incidental re-classify: a successful load (Ready/Empty), OR an
    /// explicit user cancel (re-listing after a cancelled CAN would re-prompt — so
    /// the user's choice to stop latches). A transient read error does NOT set this
    /// (it stays re-fetchable). Cleared on a card (re)bind (resetListState), so a
    /// card swap / reinsert re-fetches.
    bool m_listSettled = false;
};

} // namespace LibreKDE::Credentials
