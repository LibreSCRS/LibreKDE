// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <Purpose/Job>
#include <Purpose/PluginBase>

#include <QObject>
#include <memory>

/// @file
/// @brief The KF6 Purpose "Sign with LibreSCRS Smart Card" Share-menu plugin.
///        A thin shell over `LibreKDE::SignJob` (agent-driven); the plugin layer
///        only adapts Purpose's `QJsonObject` data + KF6 dialogs to the SDK-free
///        core.

namespace LibreKDE {
class AgentClient;
class SignJob;
} // namespace LibreKDE

namespace LibreKDE::Purpose {

/// @brief One sharing run: resolves a signing card+cert via the agent and signs
///        the input URL, writing the artifact next to it.
///
/// Purpose hands the job its inputs through `data()` (a `QJsonObject`): `urls`
/// (a single-element array — the file to sign) and `mimeType`. The job picks a
/// PKI-capable card from `AgentClient`, wires the KF6 cert-chooser and
/// overwrite-confirm dialogs into a `LibreKDE::SignJob`, runs it, and reports
/// the output path through `setOutput` + `emitResult` (or `setError`).
class SignPurposeJob : public ::Purpose::Job
{
    Q_OBJECT
public:
    explicit SignPurposeJob(QObject* parent = nullptr);
    ~SignPurposeJob() override;

    void start() override;

private:
    void finishWithError(int code, const QString& message);

    std::shared_ptr<LibreKDE::AgentClient> m_client;
    LibreKDE::SignJob* m_signJob = nullptr;
};

/// @brief Plugin entry point — Purpose calls `createJob()` per share action.
class SignPurposePlugin : public ::Purpose::PluginBase
{
    Q_OBJECT
public:
    using ::Purpose::PluginBase::PluginBase;

    [[nodiscard]] ::Purpose::Job* createJob() const override;
};

} // namespace LibreKDE::Purpose
