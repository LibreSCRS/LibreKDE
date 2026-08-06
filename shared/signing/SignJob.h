// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CertSelector.h"
#include "MimeFormatMap.h"

#include <LibreSCRS/AgentClient/OperationPhase.h> // OperationPhase (phaseChanged relay type)

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>

/// @file
/// @brief The agent-driven signing core behind the Purpose plugin. Purpose-SDK
///        free so it is unit-testable against the FakeAgent.

namespace LibreSCRS::AgentClient {
class AgentCard;
}

namespace LibreKDE {

/// @brief Signs one input file via the agent and writes the artifact next to it.
///
/// Lifecycle (all over the shared agent client library, never raw D-Bus):
/// enumerate the card's signing certs (`AgentCard::readCertificates`) → filter
/// `signingCapable` → auto-pick a lone cert or defer to the injected
/// `CertChooser` (none → `CapabilityMissing`) → open the input read-only →
/// `AgentCard::sign(certId, fd, options)` (the format/packaging come from
/// `MimeFormatMap::resolve` on the input MIME, or a caller override) → on a
/// terminal `Ok` write `takeSignedArtifact()` to `MimeFormatMap::outputName(...)`
/// beside the input (guarded by the injected `OverwriteConfirmer`; the input is
/// NEVER modified in place) → emit `succeeded(outputPath)`; otherwise
/// `failed(message)` with the agent-mapped `ErrorText`. The forwarded options
/// carry this client's non-secret format/packaging choice plus the typed
/// options' baseline level; no timestamp authority and no visible-signature
/// placement are requested, so those stay the agent's own configuration.
class SignJob : public QObject
{
    Q_OBJECT
public:
    /// @param card        the chosen signing-capable card (caller-owned).
    /// @param inputPath   absolute path to the document to sign.
    /// @param mimeType    source MIME; empty → sniffed from @p inputPath.
    /// @param formatOverride optional per-request format; empty
    ///        → derived from the MIME. Only the AdES formats the agent's option
    ///        vocabulary admits are honoured; anything else fails the job
    ///        before any signing starts.
    SignJob(LibreSCRS::AgentClient::AgentCard* card, QString inputPath, QString mimeType, QString formatOverride,
            CertChooser chooser, OverwriteConfirmer overwriteConfirmer, QObject* parent = nullptr);
    ~SignJob() override;

    SignJob(const SignJob&) = delete;
    SignJob& operator=(const SignJob&) = delete;

    /// @brief Begin the flow. Emits exactly one of `succeeded`/`failed`.
    void start();

    /// @brief The resolved output path (valid after `succeeded`).
    [[nodiscard]] QString outputPath() const;

    /// @brief The agent's own metadata for the produced signature — the
    ///        `sign-meta` map (`format`, `level`, `tsaUsed`, `chainComplete`).
    ///        Valid after `succeeded`; empty otherwise.
    ///
    /// The resolved level is not necessarily the level requested: a request
    /// that defers resolves against the agent's configured default, so this is
    /// the only honest answer to what was actually produced.
    [[nodiscard]] QVariantMap signMeta() const;

Q_SIGNALS:
    void succeeded(const QString& outputPath);
    void failed(const QString& message);
    /// @brief Relays the ACTIVE agent operation's phase (the cert enumeration
    ///        op, then the sign op) so a host can drive a spinner + phase label
    ///        across the whole flow. The two ops never emit concurrently (the
    ///        cert op is deleteLater'd before the sign op is created).
    void phaseChanged(LibreSCRS::AgentClient::OperationPhase phase, double progress);

private:
    // The terminal outcome is polled off the operation that reported it
    // (`status()` / `errorCode()` / `messageFallback()`), so these slots take no
    // arguments: the Job already holds the operation, and reads the agent's own
    // specific message from it to forward to ErrorText.
    void onCertificatesFinished();
    void beginSign(const QString& certId);
    void onSignFinished();
    void fail(const QString& message);

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace LibreKDE
