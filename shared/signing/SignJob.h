// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "AgentOperation.h" // LibreKDE::OperationPhase (phaseChanged relay type)
#include "CertSelector.h"
#include "MimeFormatMap.h"

#include <QObject>
#include <QString>
#include <memory>

/// @file
/// @brief The agent-driven signing core behind the Purpose plugin. Purpose-SDK
///        free so it is unit-testable against the FakeAgent.

namespace LibreKDE {

class AgentCard;
class AgentOperation;

/// @brief Signs one input file via the agent and writes the artifact next to it.
///
/// Lifecycle (all over `librekde-agentclient`, never raw D-Bus): enumerate the
/// card's signing certs (`AgentCard::readCertificates`) → filter
/// `signingCapable` → auto-pick a lone cert or defer to the injected
/// `CertChooser` (none → `CapabilityMissing`) → open the input read-only →
/// `AgentCard::sign(certId, fd, {format,packaging})` (the format/packaging come
/// from `MimeFormatMap::resolve` on the input MIME, or a caller override) → on
/// `finished(Ok)` write `signResult().artifact` to
/// `MimeFormatMap::outputName(...)` beside the input (guarded by the injected
/// `OverwriteConfirmer`; the input is NEVER modified in place) → emit
/// `succeeded(outputPath)`; otherwise `failed(message)` with the agent-mapped
/// `ErrorText`. Level/TSA/trust are the agent's Config1 defaults — this
/// forwards only the non-secret format/packaging choice.
class SignJob : public QObject
{
    Q_OBJECT
public:
    /// @param card        the chosen signing-capable card (caller-owned).
    /// @param inputPath   absolute path to the document to sign.
    /// @param mimeType    source MIME; empty → sniffed from @p inputPath.
    /// @param formatOverride optional per-request format; empty
    ///        → derived from the MIME. Out-of-vocabulary values are forwarded
    ///        verbatim and rejected by the agent.
    SignJob(AgentCard* card, QString inputPath, QString mimeType, QString formatOverride, CertChooser chooser,
            OverwriteConfirmer overwriteConfirmer, QObject* parent = nullptr);
    ~SignJob() override;

    SignJob(const SignJob&) = delete;
    SignJob& operator=(const SignJob&) = delete;

    /// @brief Begin the flow. Emits exactly one of `succeeded`/`failed`.
    void start();

    /// @brief The resolved output path (valid after `succeeded`).
    [[nodiscard]] QString outputPath() const;

Q_SIGNALS:
    void succeeded(const QString& outputPath);
    void failed(const QString& message);
    /// @brief Relays the ACTIVE agent operation's phase (the cert enumeration
    ///        op, then the sign op) so a host can drive a spinner + phase label
    ///        across the whole flow. The two ops never emit concurrently (the
    ///        cert op is deleteLater'd before the sign op is created).
    void phaseChanged(LibreKDE::OperationPhase phase, double progress);

private:
    // The finished-signal args carry the agent's msgFallback (AgentOperation
    // has no accessor for it), so the terminal slots take them and forward the
    // specific message to ErrorText — mirroring SmartCardHandler::onOperationFinished.
    void onCertificatesFinished(LibreKDE::OperationStatus status, LibreKDE::ErrorCode errorCode, const QString& msgKey,
                                const QString& msgFallback);
    void beginSign(const QString& certId);
    void onSignFinished(LibreKDE::OperationStatus status, LibreKDE::ErrorCode errorCode, const QString& msgKey,
                        const QString& msgFallback);
    void fail(const QString& message);

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace LibreKDE
