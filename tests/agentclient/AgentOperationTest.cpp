// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// AgentOperation: happy path, the Finished-before-Result race recovered via
// GetResult, the error path, and Cancel().

#include "AgentCard.h"
#include "AgentCapabilities.h"
#include "AgentOperation.h"
#include "CredentialTypes.h"
#include "ErrorText.h"
#include "TestBus.h"

#include <QDBusUnixFileDescriptor>
#include <QSignalSpy>
#include <gtest/gtest.h>
#include <tuple>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
// Read the full contents of a sealed-artifact fd via mmap (the recipient
// contract: mmap-read then close).
QByteArray readArtifact(int fd)
{
    struct stat st{};
    if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
        return {};
    }
    void* p = ::mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
        return {};
    }
    QByteArray out(static_cast<const char*>(p), static_cast<int>(st.st_size));
    ::munmap(p, static_cast<size_t>(st.st_size));
    return out;
}
} // namespace

using namespace LibreKDE;
using namespace LibreKDETest;

namespace {
AgentOperation* startSign(AgentCard& card)
{
    int fd = ::open("/dev/null", O_RDONLY);
    EXPECT_GE(fd, 0);
    AgentOperation* op = card.sign(QStringLiteral("certid"), QDBusUnixFileDescriptor(fd), {});
    ::close(fd);
    return op;
}
} // namespace

TEST(AgentOperation, HappyPathResultThenFinishedOk)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.operationDelayMs = 20; // result + finished arrive after we subscribe
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = startSign(card);
    ASSERT_NE(op, nullptr);

    QSignalSpy resultSpy(op, &AgentOperation::signResultReady);
    QSignalSpy finishedSpy(op, &AgentOperation::finished);

    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_EQ(op->status(), OperationStatus::Ok);
    EXPECT_GE(resultSpy.count(), 1);
    EXPECT_GE(finishedSpy.count(), 1);
    EXPECT_TRUE(op->signResult().artifact.isValid());
}

TEST(AgentOperation, LateSubscribeRaceRecoversViaGetResult)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.raceResultBeforeReturn = true; // op fires Result + Finished before Sign() returns the path
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    // By the time sign() returns the path and AgentOperation subscribes, the
    // fake has ALREADY emitted Result + Finished. The terminal-triple probe in
    // the AgentOperation ctor must recover the outcome AND pull the artifact
    // through GetResult().
    AgentOperation* op = startSign(card);
    ASSERT_NE(op, nullptr);

    EXPECT_TRUE(op->isFinished()); // recovered synchronously in the ctor
    EXPECT_EQ(op->status(), OperationStatus::Ok);
    ASSERT_TRUE(op->signResult().artifact.isValid()) << "GetResult recovery did not surface the artifact fd";

    // byte-correctness — the recovered fd must carry the scripted artifact,
    // and the recovered meta must carry the resolved fields.
    EXPECT_EQ(readArtifact(op->signResult().artifact.fileDescriptor()), QByteArrayLiteral("FAKE-SIGNED-ARTIFACT"));
    EXPECT_EQ(op->signResult().meta.value(QStringLiteral("format")).toString(), QStringLiteral("pades"));
    EXPECT_EQ(op->signResult().meta.value(QStringLiteral("level")).toString(), QStringLiteral("b-b"));
}

TEST(AgentOperation, ErrorPathSurfacesErrorCode)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.operationDelayMs = 10;
    cfg.finalStatus = 2;     // Error
    cfg.finalErrorCode = 17; // RateLimited
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = startSign(card);
    ASSERT_NE(op, nullptr);

    OperationStatus seenStatus = OperationStatus::Ok;
    ErrorCode seenCode = ErrorCode::None;
    QString seenFallback;
    QObject::connect(op, &AgentOperation::finished, op,
                     [&](OperationStatus s, ErrorCode c, const QString&, const QString& fb) {
                         seenStatus = s;
                         seenCode = c;
                         seenFallback = fb;
                     });

    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_EQ(seenStatus, OperationStatus::Error);
    EXPECT_EQ(seenCode, ErrorCode::RateLimited);
    EXPECT_FALSE(op->signResult().artifact.isValid());

    const QString text = ErrorText::forCode(seenCode, seenFallback);
    EXPECT_FALSE(text.isEmpty());
}

// a non-Sign op (Identity) that finishes Ok but whose typed Result was
// never delivered (lost/late signal) must fail LOUDLY — Identity has no
// GetResult, so a silent finished(Ok) would hand the caller an empty field map
// indistinguishable from a genuinely empty read. We surface a CommunicationError
// instead, and the identity result stays empty.
TEST(AgentOperation, NonSignLostResultFinishesLoudlyNotSilentlyEmpty)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.operationDelayMs = 10;
    cfg.suppressResult = true; // finish Ok WITHOUT emitting the Identity Result
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = card.readIdentity();
    ASSERT_NE(op, nullptr);

    bool identitySeen = false;
    QObject::connect(op, &AgentOperation::identityResultReady, op, [&]() { identitySeen = true; });

    OperationStatus seenStatus = OperationStatus::Ok;
    ErrorCode seenCode = ErrorCode::None;
    QObject::connect(op, &AgentOperation::finished, op,
                     [&](OperationStatus s, ErrorCode c, const QString&, const QString&) {
                         seenStatus = s;
                         seenCode = c;
                     });

    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_FALSE(identitySeen) << "no Result was emitted, so identityResultReady must NOT fire";
    EXPECT_EQ(seenStatus, OperationStatus::Error) << "lost Result must surface as a failure, not a silent Ok";
    EXPECT_EQ(seenCode, ErrorCode::CommunicationError);
    EXPECT_TRUE(op->identityResult().isEmpty());
    // The op's own cached status mirrors the loud terminal, not the wire's Ok.
    EXPECT_EQ(op->status(), OperationStatus::Error);
}

// A variant via the late-subscribe terminal-triple path: the Identity op
// finishes Ok before the client subscribes (no Result on the wire, no GetResult
// on Identity), so the ctor recovery must ALSO fail loudly.
TEST(AgentOperation, NonSignLateSubscribeLostResultIsLoud)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.raceResultBeforeReturn = true; // finish before Sign()/ReadIdentity() returns the path
    cfg.suppressResult = true;
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = card.readIdentity();
    ASSERT_NE(op, nullptr);

    EXPECT_TRUE(op->isFinished()); // recovered synchronously in the ctor
    EXPECT_EQ(op->status(), OperationStatus::Error);
    EXPECT_EQ(op->errorCode(), ErrorCode::CommunicationError);
    EXPECT_TRUE(op->identityResult().isEmpty());
}

// A Sign op that finishes Ok but whose Result is lost AND whose GetResult yields
// nothing (NoResult / grace window elapsed) must not claim Ok with a null fd:
// signResult stays null and the outcome is reported as a failure, not a crash.
TEST(AgentOperation, SignLostResultAndGetResultNoResultIsLoudNotCrash)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.raceResultBeforeReturn = true; // finish before Sign() returns the path
    cfg.suppressResult = true;         // no Result + no kept artifact → GetResult returns NoResult
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = startSign(card);
    ASSERT_NE(op, nullptr);

    EXPECT_TRUE(op->isFinished());
    EXPECT_FALSE(op->signResult().artifact.isValid()) << "GetResult returned NoResult; the fd must stay null";
    EXPECT_EQ(op->status(), OperationStatus::Error);
    EXPECT_EQ(op->errorCode(), ErrorCode::CommunicationError);
}

// A GetPhoto op delivers the Photo1 `a{sh}` sealed-memfd map. The
// FakeAgent scripts one entry ("personal:photo") holding known bytes; assert
// photoResultReady fires and the recovered fd reads back exactly those bytes.
TEST(AgentOperation, GetPhotoDeliversSealedMemfdMap)
{
    const QByteArray kPhotoBytes = QByteArrayLiteral("\x89PNG\r\n\x1a\n-fake-image-bytes");

    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.operationDelayMs = 20; // result + finished arrive after we subscribe
    cfg.photoBytes = kPhotoBytes;
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = card.getPhoto();
    ASSERT_NE(op, nullptr);

    QSignalSpy photoSpy(op, &AgentOperation::photoResultReady);

    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_EQ(op->status(), OperationStatus::Ok);
    EXPECT_GE(photoSpy.count(), 1);

    const PhotoMap& photos = op->photoResult();
    ASSERT_TRUE(photos.contains(QStringLiteral("personal:photo")));
    const QDBusUnixFileDescriptor& fd = photos.value(QStringLiteral("personal:photo"));
    ASSERT_TRUE(fd.isValid());
    EXPECT_EQ(readArtifact(fd.fileDescriptor()), kPhotoBytes);
}

// --- Late-subscriber GetResult recovery for the inline typed results --------
//
// Identity1/Certificates1/Photo1 gained a GetResult pull mirroring Sign1's:
// when the one-shot typed Result is lost/raced but the op finished Ok, the
// client re-serves the retained payload via GetResult instead of the old loud
// CommunicationError. The FakeAgent's lostSignalRecoverable mode models this
// deterministically — the op finishes Ok, NEVER signals the Result, and its
// GetResult serves the full payload.

// Terminal-Ok-without-Result path: the client subscribes FIRST (operationDelayMs),
// the op finishes Ok with no Result signal, and finalizeTerminal recovers the
// identity field map via Identity1.GetResult.
TEST(AgentOperation, IdentityLostSignalRecoversViaGetResult)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.operationDelayMs = 20;        // client subscribes before the op fires
    cfg.lostSignalRecoverable = true; // finish Ok, no Result signal, GetResult serves
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = card.readIdentity();
    ASSERT_NE(op, nullptr);

    QSignalSpy identitySpy(op, &AgentOperation::identityResultReady);
    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_EQ(op->status(), OperationStatus::Ok) << "the lost Result is recovered, so the read is Ok, not loud-failed";
    EXPECT_GE(identitySpy.count(), 1) << "GetResult recovery must fire identityResultReady";
    const IdentityFields& fields = op->identityResult();
    ASSERT_TRUE(fields.contains(QStringLiteral("personal")));
    EXPECT_EQ(fields.value(QStringLiteral("personal")).value(QStringLiteral("given_name")).value.variant().toString(),
              QStringLiteral("Ana"));
}

// Ctor recovery path: the op finishes Ok before readIdentity() returns the path
// (raceResultBeforeReturn) AND never signals the Result (lostSignalRecoverable),
// so the AgentOperation ctor's terminal-triple probe recovers the payload via
// Identity1.GetResult synchronously.
TEST(AgentOperation, IdentityLostSignalCtorPathRecoversViaGetResult)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.raceResultBeforeReturn = true; // finished before the path returns
    cfg.lostSignalRecoverable = true;  // and never signalled the Result
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = card.readIdentity();
    ASSERT_NE(op, nullptr);

    EXPECT_TRUE(op->isFinished()); // recovered synchronously in the ctor
    EXPECT_EQ(op->status(), OperationStatus::Ok);
    ASSERT_TRUE(op->identityResult().contains(QStringLiteral("personal")))
        << "the ctor recovery must pull the identity map via Identity1.GetResult";
}

TEST(AgentOperation, CertificatesLostSignalRecoversViaGetResult)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-A"), true, QStringLiteral("Ana Signer")}};
    cfg.operationDelayMs = 20;
    cfg.lostSignalRecoverable = true;
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = card.readCertificates();
    ASSERT_NE(op, nullptr);

    QSignalSpy certSpy(op, &AgentOperation::certificatesResultReady);
    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_EQ(op->status(), OperationStatus::Ok);
    EXPECT_GE(certSpy.count(), 1) << "GetResult recovery must fire certificatesResultReady";
    const CertificateList& certs = op->certificatesResult();
    ASSERT_EQ(certs.size(), 1);
    EXPECT_EQ(certs.at(0).certId, QStringLiteral("cert-A"));
    EXPECT_EQ(certs.at(0).subjectCn, QStringLiteral("Ana Signer"));
}

// Photo recovery re-serves a REAL sealed memfd (re-sealed from the retained
// bytes) — the recovered fd must read back exactly the scripted image bytes.
TEST(AgentOperation, PhotoLostSignalRecoversViaGetResultWithSealedMemfd)
{
    const QByteArray kPhotoBytes = QByteArrayLiteral("\x89PNG\r\n\x1a\n-recovered-image-bytes");

    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.operationDelayMs = 20;
    cfg.photoBytes = kPhotoBytes;
    cfg.lostSignalRecoverable = true;
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = card.getPhoto();
    ASSERT_NE(op, nullptr);

    QSignalSpy photoSpy(op, &AgentOperation::photoResultReady);
    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_EQ(op->status(), OperationStatus::Ok);
    EXPECT_GE(photoSpy.count(), 1) << "GetResult recovery must fire photoResultReady";
    const PhotoMap& photos = op->photoResult();
    ASSERT_TRUE(photos.contains(QStringLiteral("personal:photo")));
    const QDBusUnixFileDescriptor& fd = photos.value(QStringLiteral("personal:photo"));
    ASSERT_TRUE(fd.isValid());
    EXPECT_EQ(readArtifact(fd.fileDescriptor()), kPhotoBytes) << "the recovered sealed memfd carries the photo bytes";
}

// Negative: the typed Result is lost AND GetResult is unavailable (NoResult —
// the same error-reply shape a version-skewed agent WITHOUT the method returns
// via UnknownMethod, handled identically by the client). The op must NOT claim
// Ok with an empty payload; it surfaces a loud CommunicationError (today's final
// fallback, preserved). Covers a non-Identity inline interface (Photo).
TEST(AgentOperation, PhotoLostResultAndGetResultUnavailableIsLoud)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.operationDelayMs = 10;
    cfg.suppressResult = true; // no Result signal AND nothing retained -> GetResult NoResult
    cfg.photoBytes = QByteArrayLiteral("unrecoverable");
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = card.getPhoto();
    ASSERT_NE(op, nullptr);

    OperationStatus seenStatus = OperationStatus::Ok;
    ErrorCode seenCode = ErrorCode::None;
    QObject::connect(op, &AgentOperation::finished, op,
                     [&](OperationStatus s, ErrorCode c, const QString&, const QString&) {
                         seenStatus = s;
                         seenCode = c;
                     });

    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_EQ(seenStatus, OperationStatus::Error) << "a lost Result with no GetResult recovery must fail loudly";
    EXPECT_EQ(seenCode, ErrorCode::CommunicationError);
    EXPECT_TRUE(op->photoResult().isEmpty());
    EXPECT_EQ(op->status(), OperationStatus::Error);
}

// --- Operation.Credentials1 typed result (the divergent interface) ----------
//
// Unlike Sign/Identity/Certificates/Photo (whose Result fires only on Ok),
// the credentials Result fires for EVERY completed attempt — including the
// "soft-fail" outcomes (invalidPin/blocked) that finish Error — and carries a
// two-out-arg payload (a{sv} result, aa{sv} records). These cases pin the four
// divergences: (1) the 2-arg slot demarshals (a{sv}, aa{sv}); (2) the Result is
// delivered on an Error terminal; (3) finalizeTerminal recovers regardless of
// Ok/Error and NEVER rewrites a real non-Ok terminal into a comms error just
// because status != Ok — the loud CommunicationError fires ONLY when there is
// genuinely no recoverable payload; (4) an empty records list is a legitimate
// mutation result, not a "no result".

// The one listed record the credentials mutation cases target: the fake
// resolves ManagePin ids against the CURRENT listing snapshot, so the mutated
// id must have been listed first. (The mutation's own Result carries no
// records — records ride ListCredentials results alone.)
QVariantMap listedUserPinRecord()
{
    return QVariantMap{{QStringLiteral("id"), QStringLiteral("user:0x86")},
                       {QStringLiteral("kind"), QStringLiteral("user")},
                       {QStringLiteral("state"), QStringLiteral("operational")}};
}

// Divergence (2)+(3): a ManagePin attempt whose Result is {invalidPin, retries_left=2}
// but whose Finished is Error(CredentialWrong). The per-attempt Result must be
// delivered, and the REAL terminal (Error/CredentialWrong) preserved — NOT
// rewritten to CommunicationError.
TEST(AgentOperation, CredentialsResultDeliveredOnErrorTerminal)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::PinManagement;
    cfg.operationDelayMs = 20; // client subscribes before the op fires the live Result
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("invalidPin")},
                                 {QStringLiteral("retries_left"), 2},
                                 {QStringLiteral("blocked"), false}};
    cfg.credRecords = {listedUserPinRecord()}; // the listing must carry the mutated id
    cfg.finalStatus = 2;                       // Error (a soft-fail: wrong PIN)
    cfg.finalErrorCode = 2;                    // CredentialWrong
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    std::ignore = card.listCredentials(); // contract: list-before-mutate (the fake enforces the agent's gate)
    AgentOperation* op = card.managePin(QStringLiteral("user:0x86"), QStringLiteral("change"), {});
    ASSERT_NE(op, nullptr);

    QSignalSpy credSpy(op, &AgentOperation::credentialsResultReady);
    QSignalSpy finishedSpy(op, &AgentOperation::finished);

    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_GE(credSpy.count(), 1) << "the per-attempt Result must fire even though Finished is Error";
    EXPECT_GE(finishedSpy.count(), 1);
    EXPECT_EQ(op->status(), OperationStatus::Error) << "the real terminal is preserved, not rewritten to a comms error";
    EXPECT_EQ(op->errorCode(), ErrorCode::CredentialWrong);
    EXPECT_EQ(op->pinResult().outcome, CredentialOutcome::InvalidPin);
    ASSERT_TRUE(op->pinResult().retriesLeft.has_value());
    EXPECT_EQ(*op->pinResult().retriesLeft, 2);
    EXPECT_FALSE(op->pinResult().blocked);
    EXPECT_TRUE(op->credentialsResult().isEmpty()) << "a mutation carries no records, only the a{sv} result";
}

// Divergence (1)+(4): ListCredentials delivers the aa{sv} records; the demarshaled
// CredentialRecord carries the typed kind/state/flags.
TEST(AgentOperation, ListCredentialsResultDeliversRecords)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::PinManagement;
    cfg.operationDelayMs = 20;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}, {QStringLiteral("blocked"), false}};
    cfg.credRecords = {QVariantMap{{QStringLiteral("id"), QStringLiteral("user:0x86")},
                                   {QStringLiteral("kind"), QStringLiteral("user")},
                                   {QStringLiteral("state"), QStringLiteral("operational")},
                                   {QStringLiteral("can_change"), true},
                                   {QStringLiteral("unblockable"), false},
                                   {QStringLiteral("activatable"), false},
                                   {QStringLiteral("key_activation_pending"), false},
                                   {QStringLiteral("key_activatable"), false},
                                   {QStringLiteral("probe_safe"), true}}};
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = card.listCredentials();
    ASSERT_NE(op, nullptr);

    QSignalSpy ready(op, &AgentOperation::credentialsResultReady);
    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_GE(ready.count(), 1);
    EXPECT_EQ(op->status(), OperationStatus::Ok);
    ASSERT_EQ(op->credentialsResult().size(), 1);
    const CredentialRecord& r = op->credentialsResult().at(0);
    EXPECT_EQ(r.id, QStringLiteral("user:0x86"));
    EXPECT_EQ(r.kind, CredentialKind::User);
    EXPECT_EQ(r.state, CredentialState::Operational);
    EXPECT_TRUE(r.canChange);
    EXPECT_TRUE(r.probeSafe);
    EXPECT_FALSE(r.unblockable);
}

// Divergence (3)+(4): the live Result signal is LOST but the payload is RETAINED,
// so finalizeTerminal recovers it via Operation.Credentials1.GetResult (the
// 2-out-arg pull). An EMPTY records list is legitimate for a mutation — the
// presence of a Result reply is the signal, not a non-empty records list — so
// the op stays Ok and credentialsResultReady still fires.
TEST(AgentOperation, CredentialsLostSignalRecoversViaGetResult)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::PinManagement;
    cfg.operationDelayMs = 20;
    cfg.lostSignalRecoverable = true; // finish, SUPPRESS the Result signal, RETAIN for GetResult
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")},
                                 {QStringLiteral("blocked"), false},
                                 {QStringLiteral("pin_activated"), true}};
    // The listing carries the mutated id; the MUTATION's own result still has
    // empty records (a legitimate mutation result — the fake, like the real
    // agent, never attaches records to a mutation Result).
    cfg.credRecords = {listedUserPinRecord()};
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    std::ignore = card.listCredentials(); // contract: list-before-mutate (the fake enforces the agent's gate)
    AgentOperation* op = card.managePin(QStringLiteral("user:0x86"), QStringLiteral("change"), {});
    ASSERT_NE(op, nullptr);

    QSignalSpy credSpy(op, &AgentOperation::credentialsResultReady);
    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_EQ(op->status(), OperationStatus::Ok) << "the lost Result is recovered via GetResult, so the op stays Ok";
    EXPECT_GE(credSpy.count(), 1) << "GetResult recovery must fire credentialsResultReady";
    EXPECT_EQ(op->pinResult().outcome, CredentialOutcome::Ok);
    ASSERT_TRUE(op->pinResult().pinActivated.has_value());
    EXPECT_TRUE(*op->pinResult().pinActivated);
    EXPECT_TRUE(op->credentialsResult().isEmpty()) << "empty-records is valid; it must not be treated as no result";
}

// Ctor-recovery path: the op finishes (Error, with a soft-fail payload) before
// managePin() returns the path AND never signals the Result, so the ctor's
// terminal-triple probe recovers the payload via GetResult synchronously and
// preserves the REAL Error terminal.
TEST(AgentOperation, CredentialsLostSignalCtorPathRecoversViaGetResult)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::PinManagement;
    cfg.raceResultBeforeReturn = true; // finished before managePin() returns the path
    cfg.lostSignalRecoverable = true;  // and never signalled the Result
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("invalidPin")},
                                 {QStringLiteral("retries_left"), 1},
                                 {QStringLiteral("blocked"), false}};
    cfg.credRecords = {listedUserPinRecord()}; // the listing must carry the mutated id
    cfg.finalStatus = 2;
    cfg.finalErrorCode = 2; // CredentialWrong
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    std::ignore = card.listCredentials(); // contract: list-before-mutate (the fake enforces the agent's gate)
    AgentOperation* op = card.managePin(QStringLiteral("user:0x86"), QStringLiteral("change"), {});
    ASSERT_NE(op, nullptr);

    EXPECT_TRUE(op->isFinished()); // recovered synchronously in the ctor
    EXPECT_EQ(op->status(), OperationStatus::Error) << "the real terminal survives ctor recovery";
    EXPECT_EQ(op->errorCode(), ErrorCode::CredentialWrong);
    EXPECT_EQ(op->pinResult().outcome, CredentialOutcome::InvalidPin);
    ASSERT_TRUE(op->pinResult().retriesLeft.has_value());
    EXPECT_EQ(*op->pinResult().retriesLeft, 1);
}

// Negative (divergence 3, the loud fallback): the Result is lost AND nothing is
// retained (GetResult -> NoResult, the same error-reply shape a version-skewed
// agent WITHOUT the method returns). Even on an Ok wire terminal, a credentials
// op with no recoverable payload is a genuine comms fault — surfaced loudly, not
// a silent empty success.
TEST(AgentOperation, CredentialsLostResultAndGetResultUnavailableIsLoud)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::PinManagement;
    cfg.operationDelayMs = 10;
    cfg.suppressResult = true; // no Result signal AND nothing retained -> GetResult NoResult
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {listedUserPinRecord()}; // the listing must carry the mutated id
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    std::ignore = card.listCredentials(); // contract: list-before-mutate (the fake enforces the agent's gate)
    AgentOperation* op = card.managePin(QStringLiteral("user:0x86"), QStringLiteral("change"), {});
    ASSERT_NE(op, nullptr);

    OperationStatus seenStatus = OperationStatus::Ok;
    ErrorCode seenCode = ErrorCode::None;
    QObject::connect(op, &AgentOperation::finished, op,
                     [&](OperationStatus s, ErrorCode c, const QString&, const QString&) {
                         seenStatus = s;
                         seenCode = c;
                     });

    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_EQ(seenStatus, OperationStatus::Error) << "a credentials op with no recoverable payload is a comms fault";
    EXPECT_EQ(seenCode, ErrorCode::CommunicationError);
    EXPECT_EQ(op->pinResult().outcome, CredentialOutcome::Unspecified);
    EXPECT_TRUE(op->credentialsResult().isEmpty());
}

TEST(AgentOperation, CancelInvokesOperationCancel)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.operationDelayMs = 5000; // never fires on its own within the test
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = startSign(card);
    ASSERT_NE(op, nullptr);
    ASSERT_FALSE(op->isFinished());

    op->cancel();

    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_EQ(op->status(), OperationStatus::Cancelled);
}
