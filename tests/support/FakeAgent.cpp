// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "FakeAgent.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusUnixFileDescriptor>
#include <QMetaMethod>
#include <QTimer>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace LibreKDETest {

// --- wire-shape mirror marshalling -----------------------------------------
// Only `<<` is exercised (the fake emits, it never demarshals a result), but
// qDBusRegisterMetaType<T>() installs BOTH operators as the type's marshaller
// pair, so both must exist for the type to have a registered signature at all.

// (sssv) — the Identity1 field tuple.
QDBusArgument& operator<<(QDBusArgument& arg, const FakeIdentityField& f)
{
    arg.beginStructure();
    arg << f.labelKey << f.labelFallback << f.type << f.value;
    arg.endStructure();
    return arg;
}
const QDBusArgument& operator>>(const QDBusArgument& arg, FakeIdentityField& f)
{
    arg.beginStructure();
    arg >> f.labelKey >> f.labelFallback >> f.type >> f.value;
    arg.endStructure();
    return arg;
}

// (ssv) — the Certificates1 field tuple (no type string, unlike Identity1's).
QDBusArgument& operator<<(QDBusArgument& arg, const FakeCertField& f)
{
    arg.beginStructure();
    arg << f.labelKey << f.labelFallback << f.value;
    arg.endStructure();
    return arg;
}
const QDBusArgument& operator>>(const QDBusArgument& arg, FakeCertField& f)
{
    arg.beginStructure();
    arg >> f.labelKey >> f.labelFallback >> f.value;
    arg.endStructure();
    return arg;
}

// (s b a{sa{s(ssv)}} u as as u) — one Certificates1 entry. The display strings
// are folded into the `fields` map here rather than appended as struct members,
// which is what keeps the struct at seven members on the wire.
QDBusArgument& operator<<(QDBusArgument& arg, const FakeCertInfo& c)
{
    FakeCertFieldGroups fields;
    if (!c.subjectCn.isEmpty()) {
        fields[QStringLiteral("subject")][QStringLiteral("cn")] = {
            QStringLiteral("label_subject_cn"), QStringLiteral("Subject CN"), QDBusVariant(c.subjectCn)};
    }
    if (!c.issuerCn.isEmpty()) {
        fields[QStringLiteral("issuer")][QStringLiteral("cn")] = {
            QStringLiteral("label_issuer_cn"), QStringLiteral("Issuer CN"), QDBusVariant(c.issuerCn)};
    }
    if (!c.notAfter.isEmpty()) {
        fields[QStringLiteral("validity")][QStringLiteral("notAfter")] = {
            QStringLiteral("label_not_after"), QStringLiteral("Not after"), QDBusVariant(c.notAfter)};
    }

    // chainSubjectCns falls back to [leaf CN] when the script left it empty (the
    // single-entry chain), so an emitted payload always carries a non-empty
    // chain like the agent's.
    const QStringList chain = c.chainSubjectCns.isEmpty() ? QStringList{c.subjectCn} : c.chainSubjectCns;

    arg.beginStructure();
    arg << c.certId << c.signingCapable << fields;
    arg << static_cast<uint>(c.keyUsageBits);
    arg << c.extendedKeyUsageOids;
    arg << chain;
    arg << static_cast<uint>(c.trustStatus);
    arg.endStructure();
    return arg;
}
const QDBusArgument& operator>>(const QDBusArgument& arg, FakeCertInfo& c)
{
    FakeCertFieldGroups fields;
    arg.beginStructure();
    arg >> c.certId >> c.signingCapable >> fields;
    uint keyUsageBits = 0;
    uint trustStatus = 0;
    arg >> keyUsageBits >> c.extendedKeyUsageOids >> c.chainSubjectCns >> trustStatus;
    c.keyUsageBits = keyUsageBits;
    c.trustStatus = trustStatus;
    arg.endStructure();

    // Each field value is a D-Bus variant `v`; the display string lives inside.
    if (const auto subj = fields.constFind(QStringLiteral("subject")); subj != fields.constEnd()) {
        c.subjectCn = subj->value(QStringLiteral("cn")).value.variant().toString();
    }
    if (const auto iss = fields.constFind(QStringLiteral("issuer")); iss != fields.constEnd()) {
        c.issuerCn = iss->value(QStringLiteral("cn")).value.variant().toString();
    }
    if (const auto val = fields.constFind(QStringLiteral("validity")); val != fields.constEnd()) {
        c.notAfter = val->value(QStringLiteral("notAfter")).value.variant().toString();
    }
    return arg;
}

namespace {
constexpr const char* kOperationIface = "org.librescrs.Agent.Operation1";
constexpr const char* kSignIface = "org.librescrs.Agent.Operation.Sign1";
constexpr const char* kIdentityIface = "org.librescrs.Agent.Operation.Identity1";
constexpr const char* kCertificatesIface = "org.librescrs.Agent.Operation.Certificates1";
constexpr const char* kPhotoIface = "org.librescrs.Agent.Operation.Photo1";

void ensureMetatypes()
{
    static bool done = false;
    if (done) {
        return;
    }
    // The fake registers its OWN mirrors and nothing else. A client registers
    // its own demarshalling types itself, on the path that needs them, so there
    // is nothing here for the fake to do on a client's behalf — and doing it
    // would tie the fake to whichever client happened to be linked.
    qDBusRegisterMetaType<FakeInterfaceProps>();
    qDBusRegisterMetaType<FakeManagedObjects>();
    qDBusRegisterMetaType<FakeIdentityField>();
    qDBusRegisterMetaType<FakeIdentityFieldGroup>();
    qDBusRegisterMetaType<FakeIdentityFields>();
    qDBusRegisterMetaType<FakeCertField>();
    qDBusRegisterMetaType<FakeCertFieldGroup>();
    qDBusRegisterMetaType<FakeCertFieldGroups>();
    qDBusRegisterMetaType<FakeCertInfo>();
    qDBusRegisterMetaType<FakeCertInfoList>();
    qDBusRegisterMetaType<FakePhotoMap>();
    // aa{sv} records for the Operation.Credentials1.Result signal + GetResult.
    qDBusRegisterMetaType<FakeCredentialRecords>();
    // ... and under the NAME moc records for it, which is a separate registry:
    // QtDBus resolves a slot's reference OUTPUT parameter by name lookup, so
    // Operation.Credentials1.GetResult's `records` out-arg is unreachable
    // without this even though the type itself is registered above. Must match
    // how the parameter is spelled in FakeCredentialsAdaptor::GetResult.
    qRegisterMetaType<FakeCredentialRecords>("LibreKDETest::FakeCredentialRecords");
    done = true;
}

// Build a sealed memfd with the given content; returns an owned fd.
int makeSealedArtifact(const QByteArray& content)
{
    int fd = memfd_create("fake-artifact", MFD_ALLOW_SEALING);
    if (fd < 0) {
        return -1;
    }
    if (!content.isEmpty()) {
        ssize_t w = ::write(fd, content.constData(), static_cast<size_t>(content.size()));
        (void)w;
    }
    ::lseek(fd, 0, SEEK_SET);
    return fd;
}
} // namespace

// --- Operation1 adaptor ----------------------------------------------------
class FakeOperationAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.librescrs.Agent.Operation1")
    Q_PROPERTY(uint Phase READ phase)
    Q_PROPERTY(double Progress READ progress)
    Q_PROPERTY(bool IsIndeterminate READ isIndeterminate)
    Q_PROPERTY(uint WatchdogTimeoutSeconds READ watchdogTimeoutSeconds)
    Q_PROPERTY(bool Completed READ completed)
    Q_PROPERTY(uint Status READ status)
    Q_PROPERTY(uint ErrorCode READ errorCode)
public:
    explicit FakeOperationAdaptor(FakeOperation* op) : QDBusAbstractAdaptor(op), m_op(op) {}

    [[nodiscard]] uint phase() const
    {
        return m_op->m_completed ? 7u : 4u;
    }
    [[nodiscard]] double progress() const
    {
        return m_op->m_completed ? 1.0 : 0.0;
    }
    [[nodiscard]] bool isIndeterminate() const
    {
        return true;
    }
    [[nodiscard]] uint watchdogTimeoutSeconds() const
    {
        return 30u;
    }
    [[nodiscard]] bool completed() const
    {
        return m_op->m_completed;
    }
    [[nodiscard]] uint status() const
    {
        return m_op->m_finalStatus;
    }
    [[nodiscard]] uint errorCode() const
    {
        return m_op->m_finalErrorCode;
    }

public Q_SLOTS:
    void Cancel()
    {
        // Record the cancel on the minting agent — the observable seam for
        // "an abandoned op was cancelled agent-side" (ops are parented to it).
        if (auto* agent = qobject_cast<FakeAgent*>(m_op->parent())) {
            agent->noteOperationCancelled();
        }
        m_op->m_finalStatus = 1u; // Cancelled
        m_op->fire();
    }

Q_SIGNALS:
    void Finished(uint status, uint errorCode, const QString& msgKey, const QString& msgFallback);

private:
    FakeOperation* m_op;
};

// --- Sign1 adaptor ---------------------------------------------------------
class FakeSignAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.librescrs.Agent.Operation.Sign1")
public:
    explicit FakeSignAdaptor(FakeOperation* op) : QDBusAbstractAdaptor(op), m_op(op) {}

public Q_SLOTS:
    // GetResult()->(h signedArtifact, a{sv} meta). The first out-arg is the
    // function return value (the fd), the second a trailing reference param —
    // matching the XML's (signedArtifact, meta) ordering.
    QDBusUnixFileDescriptor GetResult(QVariantMap& meta)
    {
        if (!m_op->m_completed || m_op->m_finalStatus != 0u || m_op->m_keptArtifactFd < 0) {
            // Frozen Sign1 contract: NoResult is a D-Bus error, not a null fd —
            // returning an invalid `h` would be unmarshalable and hang the peer.
            m_op->replyNoResult();
            meta = QVariantMap{};
            return QDBusUnixFileDescriptor();
        }
        meta = m_op->m_signMeta;
        return QDBusUnixFileDescriptor(m_op->m_keptArtifactFd);
    }

Q_SIGNALS:
    void Result(const QDBusUnixFileDescriptor& signedArtifact, const QVariantMap& meta);

private:
    FakeOperation* m_op;
};

// --- Identity1 adaptor -----------------------------------------------------
class FakeIdentityAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.librescrs.Agent.Operation.Identity1")
public:
    explicit FakeIdentityAdaptor(FakeOperation* op) : QDBusAbstractAdaptor(op), m_op(op) {}

public Q_SLOTS:
    // GetResult()->a{sa{s(sssv)}}: re-serve the retained identity field map (the
    // late-subscriber recovery pull). NoResult is a D-Bus error, not an empty
    // map, mirroring the frozen contract + FakeSignAdaptor.
    FakeIdentityFields GetResult()
    {
        if (!m_op->m_resultRetained) {
            m_op->replyNoResult();
            return {};
        }
        return m_op->buildIdentityFields();
    }

Q_SIGNALS:
    void Result(const FakeIdentityFields& fields);

private:
    FakeOperation* m_op;
};

// --- Photo1 adaptor --------------------------------------------------------
// Emits the real wire shape a{sh} (group:field -> sealed memfd) so the client's
// PhotoMap demarshaller (the production code under test) parses it exactly as it
// would a real agent's sealed-memfd payload.
class FakePhotoAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.librescrs.Agent.Operation.Photo1")
public:
    explicit FakePhotoAdaptor(FakeOperation* op) : QDBusAbstractAdaptor(op), m_op(op) {}

public Q_SLOTS:
    // GetResult()->a{sh}: re-serve the retained photo(s) as FRESH sealed-memfd
    // dups (the agent retains raw bytes it re-seals per call). NoResult is a
    // D-Bus error, mirroring the frozen contract + FakeSignAdaptor.
    FakePhotoMap GetResult()
    {
        if (!m_op->m_resultRetained) {
            m_op->replyNoResult();
            return {};
        }
        m_op->retainPhotoFd(); // idempotent — ensure the sealed source fd exists
        FakePhotoMap photos;
        if (m_op->m_keptPhotoFd >= 0) {
            const int dup = ::dup(m_op->m_keptPhotoFd);
            photos.insert(QStringLiteral("personal:photo"), QDBusUnixFileDescriptor(dup));
            if (dup >= 0) {
                ::close(dup);
            }
        }
        return photos;
    }

Q_SIGNALS:
    void Result(const FakePhotoMap& photos);

private:
    FakeOperation* m_op;
};

// --- Certificates1 adaptor -------------------------------------------------
// Emits the real wire struct a(sba{sa{s(ssv)}}uasasu); the marshaller below
// builds it from the scripted FakeCertList so the client's CertificateInfo
// demarshaller (the production code under test) parses it exactly as it would a
// real agent's payload — subject/cn drives the chooser label.
class FakeCertificatesAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.librescrs.Agent.Operation.Certificates1")
public:
    explicit FakeCertificatesAdaptor(FakeOperation* op) : QDBusAbstractAdaptor(op), m_op(op) {}

public Q_SLOTS:
    // GetResult()->a(sba{sa{s(ssv)}}uasasu): re-serve the retained certificate
    // list (the late-subscriber recovery pull). NoResult is a D-Bus error,
    // mirroring the frozen contract + FakeSignAdaptor.
    FakeCertInfoList GetResult()
    {
        if (!m_op->m_resultRetained) {
            m_op->replyNoResult();
            return {};
        }
        return m_op->buildCertificateList();
    }

Q_SIGNALS:
    void Result(const FakeCertInfoList& certificates);

private:
    FakeOperation* m_op;
};

// --- Operation.Credentials1 adaptor ----------------------------------------
// Emits the real wire shape (a{sv} result, aa{sv} records) so the client's
// two-arg onCredentialsResult slot + recoverCredentialsResultViaGetResult (the
// production code under test) parse it exactly as they would a real agent's
// payload. Unlike the Ok-only Sign/Identity/Certificates/Photo results, the
// credentials Result fires for EVERY completed attempt (Ok AND the soft-fail
// invalidPin/blocked outcomes that finish Error).
class FakeCredentialsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.librescrs.Agent.Operation.Credentials1")
public:
    explicit FakeCredentialsAdaptor(FakeOperation* op) : QDBusAbstractAdaptor(op), m_op(op) {}

public Q_SLOTS:
    // GetResult()->(a{sv} result, aa{sv} records): re-serve the retained payload
    // (the late-subscriber recovery pull). The first out-arg is the function
    // return value (result), the second a trailing reference param (records) —
    // matching the XML's (result, records) ordering + FakeSignAdaptor. NoResult
    // is a D-Bus error, not an empty map, mirroring the frozen contract.
    //
    // `records` is spelled with its full namespace on purpose: QtDBus resolves a
    // slot's non-const-reference OUTPUT parameter by looking up the type NAME moc
    // recorded for it, so that name must be one ensureMetatypes() registered. A
    // spelling neither side agrees on makes QtDBus find no matching slot and
    // answer the call with an error instead of the payload.
    QVariantMap GetResult(LibreKDETest::FakeCredentialRecords& records)
    {
        if (!m_op->m_resultRetained) {
            m_op->replyNoResult();
            records = {};
            return {};
        }
        records = m_op->m_credRecords;
        return m_op->m_credResult;
    }

Q_SIGNALS:
    void Result(const QVariantMap& result, const FakeCredentialRecords& records);

private:
    FakeOperation* m_op;
};

// --- FakeOperation ---------------------------------------------------------
FakeOperation::FakeOperation(QObject* parent, QDBusConnection connection, QString path, Kind kind, int delayMs,
                             uint finalStatus, uint finalErrorCode, bool suppressResult, FakeCertList certScript,
                             bool rawCertResult, QByteArray photoBytes, bool photoEmptyMap, bool announceConsentPhase,
                             bool lostSignalRecoverable, QVariantMap credResult, FakeCredentialRecords credRecords)
    : QObject(parent), m_connection(connection), m_path(std::move(path)), m_kind(kind), m_delayMs(delayMs),
      m_finalStatus(finalStatus), m_finalErrorCode(finalErrorCode), m_suppressResult(suppressResult),
      m_lostSignalRecoverable(lostSignalRecoverable), m_certScript(std::move(certScript)),
      m_rawCertResult(rawCertResult), m_photoBytes(std::move(photoBytes)), m_photoEmptyMap(photoEmptyMap),
      m_announceConsentPhase(announceConsentPhase), m_credResult(std::move(credResult)),
      m_credRecords(std::move(credRecords))
{
    m_opAdaptor = std::make_unique<FakeOperationAdaptor>(this);
    if (m_kind == Kind::Sign) {
        m_resultAdaptor.reset(new FakeSignAdaptor(this));
    } else if (m_kind == Kind::Certificates) {
        m_resultAdaptor.reset(new FakeCertificatesAdaptor(this));
    } else if (m_kind == Kind::Photo) {
        m_resultAdaptor.reset(new FakePhotoAdaptor(this));
    } else if (m_kind == Kind::Credentials) {
        m_resultAdaptor.reset(new FakeCredentialsAdaptor(this));
    } else {
        m_resultAdaptor.reset(new FakeIdentityAdaptor(this));
    }
    m_connection.registerObject(m_path, this);
}

FakeOperation::~FakeOperation()
{
    if (m_keptArtifactFd >= 0) {
        ::close(m_keptArtifactFd);
    }
    if (m_keptPhotoFd >= 0) {
        ::close(m_keptPhotoFd);
    }
}

QString FakeOperation::path() const
{
    return m_path;
}

QVariantMap FakeOperation::defaultSignMeta()
{
    return QVariantMap{{QStringLiteral("format"), QStringLiteral("pades")},
                       {QStringLiteral("level"), QStringLiteral("b-b")},
                       {QStringLiteral("tsaUsed"), false},
                       {QStringLiteral("chainComplete"), false}};
}

void FakeOperation::setSignMeta(QVariantMap meta)
{
    m_signMeta = std::move(meta);
}

void FakeOperation::replyNoResult()
{
    setDelayedReply(true); // discard the adaptor's (null-fd) return value
    QDBusContext::connection().send(message().createErrorReply(QStringLiteral("org.librescrs.Agent.Error.NoResult"),
                                                               QStringLiteral("grace window elapsed / not Ok")));
}

void FakeOperation::start()
{
    // Announce AwaitingConsent AFTER a short delay so the signal lands once the
    // client has subscribed to this op's PropertiesChanged (the client only
    // connects after Card1.<Method> returns this op's path). 50 ms comfortably
    // clears the local-bus AddMatch latency and stays well under any test's
    // injected op-stall timeout.
    if (m_announceConsentPhase) {
        QTimer::singleShot(50, this, [this]() { emitPhase(2u /* OperationPhase::AwaitingConsent */); });
    }
    if (m_delayMs <= 0) {
        fire();
    } else {
        QTimer::singleShot(m_delayMs, this, [this]() { fire(); });
    }
}

void FakeOperation::emitPhase(uint phase)
{
    QDBusMessage sig = QDBusMessage::createSignal(m_path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                  QStringLiteral("PropertiesChanged"));
    QVariantMap changed;
    changed.insert(QStringLiteral("Phase"), phase);
    sig << QStringLiteral("org.librescrs.Agent.Operation1") << changed << QStringList{};
    m_connection.send(sig);
}

void FakeOperation::fire()
{
    if (m_completed) {
        return;
    }

    // An Ok op RETAINS its payload (so GetResult can re-serve it, mirroring the
    // agent's recovery store published on emitResult) and normally EMITS the
    // typed Result BEFORE Finished (strict ordering). Two loss modes:
    //   * suppressResult      — no retain, no signal, GetResult -> NoResult
    //                           (the "GetResult also unavailable" negative case).
    //   * lostSignalRecoverable — RETAIN but do NOT signal: the deterministic
    //                           lost-signal race the client recovers via GetResult.
    if (m_kind == Kind::Credentials) {
        // The Credentials Result fires for EVERY completed attempt — Ok AND the
        // soft-fail invalidPin/blocked outcomes that finish Error — because the
        // a{sv} payload carries the per-attempt outcome, unlike the Ok-only
        // Sign/Identity/Certificates/Photo results. Retain it for GetResult
        // unless total loss (suppressResult -> NoResult); emit the live signal
        // unless the deterministic lost-signal race (lostSignalRecoverable:
        // retain but do NOT signal).
        const bool credRetained = !m_suppressResult;
        const bool credEmitSignal = credRetained && !m_lostSignalRecoverable;
        if (credRetained) {
            m_resultRetained = true; // GetResult now serves (retained payload)
            if (credEmitSignal) {
                Q_EMIT static_cast<FakeCredentialsAdaptor*>(m_resultAdaptor.get())->Result(m_credResult, m_credRecords);
            }
        }
    } else {
        const bool okResult = (m_finalStatus == 0u && !m_suppressResult);
        const bool emitSignal = okResult && !m_lostSignalRecoverable;
        if (okResult) {
            m_resultRetained = true; // GetResult now serves (retained payload)
            if (m_kind == Kind::Sign) {
                m_keptArtifactFd = makeSealedArtifact(QByteArrayLiteral("FAKE-SIGNED-ARTIFACT"));
                if (emitSignal) {
                    int dup = m_keptArtifactFd >= 0 ? ::dup(m_keptArtifactFd) : -1;
                    Q_EMIT static_cast<FakeSignAdaptor*>(m_resultAdaptor.get())
                        ->Result(QDBusUnixFileDescriptor(dup), m_signMeta);
                    if (dup >= 0) {
                        ::close(dup);
                    }
                }
            } else if (m_kind == Kind::Certificates) {
                if (emitSignal) {
                    if (m_rawCertResult) {
                        emitRawCertResult();
                    } else {
                        Q_EMIT static_cast<FakeCertificatesAdaptor*>(m_resultAdaptor.get())
                            ->Result(buildCertificateList());
                    }
                }
            } else if (m_kind == Kind::Photo) {
                retainPhotoFd(); // seal the bytes so GetResult can re-dup, signal or not
                if (emitSignal) {
                    emitPhotoResult();
                }
            } else {
                // Identity: a{sa{s(sssv)}} with one group/one field, via the typed
                // adaptor signal (auto-relayed using the registered metatype).
                if (emitSignal) {
                    Q_EMIT static_cast<FakeIdentityAdaptor*>(m_resultAdaptor.get())->Result(buildIdentityFields());
                }
            }
        }
    }

    m_completed = true;

    // Finished.
    QString msgKey = m_finalStatus == 2u ? QStringLiteral("err.key") : QString();
    QString msgFallback = m_finalStatus == 2u ? QStringLiteral("agent fallback") : QString();
    Q_EMIT m_opAdaptor->Finished(m_finalStatus, m_finalErrorCode, msgKey, msgFallback);
}

void FakeOperation::emitRawCertResult()
{
    // Hand-build a(sba{sa{s(ssv)}}uasasu) with raw beginStructure/beginMap calls
    // — deliberately NOT the client's operator<<, so operator>> is exercised
    // against a foreign-marshalled payload. Sent over the real bus so Qt's
    // marshaller produces a genuine demarshalable message (a standalone
    // QDBusArgument does not round-trip write->read).
    const auto writeField = [](QDBusArgument& a, const QString& labelKey, const QString& fallback,
                               const QString& value) {
        a.beginStructure();
        a << labelKey << fallback << QDBusVariant(value);
        a.endStructure();
    };
    const auto writeGroup = [&writeField](QDBusArgument& a, const QString& group, const QString& fieldKey,
                                          const QString& labelKey, const QString& fallback, const QString& value) {
        a.beginMapEntry();
        a << group;
        a.beginMap(QMetaType::fromType<QString>().id(), qMetaTypeId<FakeCertField>());
        a.beginMapEntry();
        a << fieldKey;
        writeField(a, labelKey, fallback, value);
        a.endMapEntry();
        a.endMap();
        a.endMapEntry();
    };

    QDBusArgument arg;
    arg.beginArray(qMetaTypeId<FakeCertInfo>());
    for (const FakeCert& fc : m_certScript) {
        arg.beginStructure(); // ( s b a{sa{s(ssv)}} u as as u )
        arg << fc.certId;
        arg << fc.signingCapable;

        arg.beginMap(QMetaType::fromType<QString>().id(), qMetaTypeId<FakeCertFieldGroup>());
        if (!fc.subjectCn.isEmpty()) {
            writeGroup(arg, QStringLiteral("subject"), QStringLiteral("cn"), QStringLiteral("label_subject_cn"),
                       QStringLiteral("Subject CN"), fc.subjectCn);
        }
        if (!fc.issuerCn.isEmpty()) {
            writeGroup(arg, QStringLiteral("issuer"), QStringLiteral("cn"), QStringLiteral("label_issuer_cn"),
                       QStringLiteral("Issuer CN"), fc.issuerCn);
        }
        if (!fc.notAfter.isEmpty()) {
            writeGroup(arg, QStringLiteral("validity"), QStringLiteral("notAfter"), QStringLiteral("label_not_after"),
                       QStringLiteral("Not after"), fc.notAfter);
        }
        arg.endMap();

        arg << fc.keyUsageBits;
        arg << fc.extendedKeyUsageOids;
        arg << fc.chainSubjectCns;
        arg << fc.trustStatus;
        arg.endStructure();
    }
    arg.endArray();

    QDBusMessage sig = QDBusMessage::createSignal(m_path, QStringLiteral("org.librescrs.Agent.Operation.Certificates1"),
                                                  QStringLiteral("Result"));
    sig << QVariant::fromValue(arg);
    m_connection.send(sig);
}

FakeIdentityFields FakeOperation::buildIdentityFields() const
{
    // a{sa{s(sssv)}} with one group/one field — the deterministic payload the
    // Identity1.Result signal AND Identity1.GetResult both serve.
    FakeIdentityField field{QStringLiteral("label_given_name"), QStringLiteral("Given name"), QStringLiteral("text"),
                            QDBusVariant(QStringLiteral("Ana"))};
    FakeIdentityFieldGroup group;
    group.insert(QStringLiteral("given_name"), field);
    FakeIdentityFields fields;
    fields.insert(QStringLiteral("personal"), group);
    return fields;
}

FakeCertInfoList FakeOperation::buildCertificateList() const
{
    FakeCertInfoList certs;
    for (const FakeCert& fc : m_certScript) {
        FakeCertInfo ci;
        ci.certId = fc.certId;
        ci.signingCapable = fc.signingCapable;
        ci.subjectCn = fc.subjectCn;
        ci.issuerCn = fc.issuerCn;
        ci.notAfter = fc.notAfter;
        ci.keyUsageBits = fc.keyUsageBits;
        ci.extendedKeyUsageOids = fc.extendedKeyUsageOids;
        ci.chainSubjectCns = fc.chainSubjectCns;
        ci.trustStatus = fc.trustStatus;
        certs.append(ci);
    }
    return certs;
}

void FakeOperation::retainPhotoFd()
{
    // Seal m_photoBytes into m_keptPhotoFd (shrink|grow|write) once, mirroring
    // the agent retaining raw bytes it re-seals per GetResult. Idempotent and
    // a no-op for the empty-map mode (no photo to retain).
    if (m_photoEmptyMap || m_keptPhotoFd >= 0) {
        return;
    }
    m_keptPhotoFd = memfd_create("fake-photo", MFD_ALLOW_SEALING);
    if (m_keptPhotoFd >= 0) {
        if (!m_photoBytes.isEmpty()) {
            ssize_t w = ::write(m_keptPhotoFd, m_photoBytes.constData(), static_cast<size_t>(m_photoBytes.size()));
            (void)w;
        }
        ::lseek(m_keptPhotoFd, 0, SEEK_SET);
        ::fcntl(m_keptPhotoFd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE);
    }
}

void FakeOperation::emitPhotoResult()
{
    // A genuinely EMPTY a{sh} (no entries): exercises the consumer's empty-MAP
    // guard (a card with no photo at all), distinct from the empty-FD guard (a
    // present-but-empty memfd entry) below.
    if (m_photoEmptyMap) {
        Q_EMIT static_cast<FakePhotoAdaptor*>(m_resultAdaptor.get())->Result(FakePhotoMap{});
        return;
    }
    // One-entry a{sh}: "personal:photo" -> a dup of the sealed source fd
    // (m_keptPhotoFd, sealed by retainPhotoFd on the retain path). The local
    // dup must outlive the send like the Sign artifact path (QDBusUnixFileDescriptor
    // dups on copy).
    retainPhotoFd();
    int dup = m_keptPhotoFd >= 0 ? ::dup(m_keptPhotoFd) : -1;
    FakePhotoMap photos;
    photos.insert(QStringLiteral("personal:photo"), QDBusUnixFileDescriptor(dup));
    Q_EMIT static_cast<FakePhotoAdaptor*>(m_resultAdaptor.get())->Result(photos);
    if (dup >= 0) {
        ::close(dup);
    }
}

// --- WedgedPropertiesAdaptor -----------------------------------------------
void WedgedPropertiesAdaptor::scriptGetAllReply(int delayMs, const QVariantMap& props)
{
    m_replyDelayMs = delayMs;
    m_scriptedProps = props;
}

int WedgedPropertiesAdaptor::getAllCallCount() const
{
    return m_getAllCalls;
}

QVariantMap WedgedPropertiesAdaptor::GetAll(const QString& /*iface*/)
{
    ++m_getAllCalls;
    // Default (unscripted): never answer — mark the call a delayed reply and
    // drop it on the floor. The client's capped GetAll must time out at
    // kPropTimeoutMs (a hung agent), not wait out the multi-second default
    // D-Bus timeout. When scripted, answer after the scripted delay with the
    // scripted a{sv} (a SLOW agent, not a hung one) — the delayed reply is sent
    // off a QTimer on this (server) thread, so the client's loop never blocks.
    // The QDBusContext lives on the registered parent CardObject, NOT on this
    // adaptor (an adaptor's own context is never populated for plain method
    // calls — same pattern as Pkcs11Adaptor/CardAdaptor). Either way the return
    // value below is discarded by setDelayedReply.
    auto* ctx = qobject_cast<CardObject*>(parent());
    if (ctx != nullptr && ctx->calledFromDBus()) {
        ctx->setDelayedReply(true);
        if (m_replyDelayMs >= 0) {
            const QDBusMessage call = ctx->message();
            const QDBusConnection replyConnection = ctx->connection();
            const QVariantMap props = m_scriptedProps;
            QTimer::singleShot(m_replyDelayMs, this, [call, replyConnection, props]() {
                QDBusConnection conn(replyConnection);
                conn.send(call.createReply(QVariant::fromValue(props)));
            });
        }
    }
    return QVariantMap{};
}

// --- Pkcs11Adaptor ---------------------------------------------------------
Pkcs11Adaptor::Pkcs11Adaptor(QObject* parent, FakeAgent* agent) : QDBusAbstractAdaptor(parent), m_agent(agent) {}

QByteArray Pkcs11Adaptor::CertDer(const QDBusObjectPath& reader, const QString& certId)
{
    m_agent->captureCertDer(reader.path(), certId);
    if (m_agent->config().certDerKeyNotFound) {
        // Mirror the agent's public-data error vocabulary: an unknown certId is
        // …Error.KeyNotFound. The QDBusContext lives on the registered root
        // object (our parent CardObject), not on the adaptor; setDelayedReply
        // there discards this return value.
        auto* ctx = qobject_cast<CardObject*>(parent());
        if (ctx && ctx->calledFromDBus()) {
            ctx->setDelayedReply(true);
            ctx->connection().send(ctx->message().createErrorReply(
                QStringLiteral("org.librescrs.Agent.Error.KeyNotFound"), QStringLiteral("unknown certId")));
        }
        return {};
    }
    return m_agent->config().certDerBytes;
}

// --- ObjectManagerAdaptor --------------------------------------------------
ObjectManagerAdaptor::ObjectManagerAdaptor(QObject* parent, FakeAgent* agent)
    : QDBusAbstractAdaptor(parent), m_agent(agent)
{}

FakeManagedObjects ObjectManagerAdaptor::GetManagedObjects()
{
    // Wedge: mark the call a delayed reply and never answer, so the client's
    // discovery call must time out on ITS side (the never-hang invariant) rather
    // than stall forever. The QDBusContext lives on the registered root object
    // (our parent CardObject), not on this adaptor — mirrors WedgedPropertiesAdaptor.
    if (m_agent->config().wedgeGetManagedObjects) {
        auto* ctx = qobject_cast<CardObject*>(parent());
        if (ctx != nullptr && ctx->calledFromDBus()) {
            ctx->setDelayedReply(true);
        }
        return FakeManagedObjects{}; // discarded by setDelayedReply
    }
    return m_agent->managedObjects();
}

// --- ReaderAdaptor ---------------------------------------------------------
ReaderAdaptor::ReaderAdaptor(QObject* parent, QString name, bool hasCard, QDBusObjectPath card)
    : QDBusAbstractAdaptor(parent), m_name(std::move(name)), m_hasCard(hasCard), m_card(std::move(card))
{}
QString ReaderAdaptor::name() const
{
    return m_name;
}
bool ReaderAdaptor::hasCard() const
{
    return m_hasCard;
}
QDBusObjectPath ReaderAdaptor::card() const
{
    return m_card;
}
void ReaderAdaptor::setHasCard(bool v)
{
    m_hasCard = v;
}
void ReaderAdaptor::setCard(QDBusObjectPath v)
{
    m_card = std::move(v);
}

// --- CardAdaptor -----------------------------------------------------------
CardAdaptor::CardAdaptor(QObject* parent, FakeAgent* agent, uint capabilities, QDBusObjectPath reader,
                         QString preReadAuth)
    : QDBusAbstractAdaptor(parent), m_agent(agent), m_capabilities(capabilities), m_reader(std::move(reader)),
      m_preReadAuth(std::move(preReadAuth))
{}
uint CardAdaptor::capabilities() const
{
    return m_capabilities;
}
QDBusObjectPath CardAdaptor::reader() const
{
    return m_reader;
}
QString CardAdaptor::preReadAuthMethod() const
{
    return m_preReadAuth;
}
void CardAdaptor::setCapabilities(uint v)
{
    m_capabilities = v;
}
void CardAdaptor::setPreReadAuthMethod(QString v)
{
    m_preReadAuth = std::move(v);
}

QDBusObjectPath CardAdaptor::sendMethodEntryError()
{
    // Mirror the agent's polkit-style precondition gate: throw at method entry
    // (no Operation minted). The QDBusContext lives on the registered CardObject
    // (our parent), not on the adaptor; setDelayedReply discards this return.
    auto* ctx = qobject_cast<CardObject*>(parent());
    if (ctx && ctx->calledFromDBus()) {
        ctx->setDelayedReply(true);
        ctx->connection().send(
            ctx->message().createErrorReply(QStringLiteral("org.librescrs.Agent.Error.UnsupportedOnThisCard"),
                                            QStringLiteral("not supported on this card")));
    }
    return QDBusObjectPath();
}

QDBusObjectPath CardAdaptor::ReadIdentity()
{
    if (m_agent->config().failMethodEntry) {
        return sendMethodEntryError();
    }
    return m_agent->mintOperation(FakeOperation::Kind::Identity);
}
QDBusObjectPath CardAdaptor::GetPhoto()
{
    if (m_agent->config().failMethodEntry) {
        return sendMethodEntryError();
    }
    return m_agent->mintOperation(FakeOperation::Kind::Photo);
}
QDBusObjectPath CardAdaptor::ReadCertificates()
{
    if (m_agent->config().failMethodEntry) {
        return sendMethodEntryError();
    }
    return m_agent->mintOperation(FakeOperation::Kind::Certificates);
}
QDBusObjectPath CardAdaptor::Sign(const QString& certId, const QDBusUnixFileDescriptor& inputFd,
                                  const QVariantMap& options)
{
    if (m_agent->config().failMethodEntry) {
        return sendMethodEntryError();
    }

    // Honor the in-args: dup + read inputFd synchronously NOW — the client
    // closes its fd as soon as Sign() unwinds, so a deferred read would race a
    // closed descriptor. Capture certId + options verbatim for the test to
    // assert exact forwarding.
    QByteArray inputBytes;
    if (inputFd.isValid()) {
        int dup = ::dup(inputFd.fileDescriptor());
        if (dup >= 0) {
            ::lseek(dup, 0, SEEK_SET);
            char buf[4096];
            ssize_t n = 0;
            while ((n = ::read(dup, buf, sizeof(buf))) > 0) {
                inputBytes.append(buf, static_cast<int>(n));
            }
            ::close(dup);
        }
    }
    m_agent->captureSign(certId, inputBytes, options);
    return m_agent->mintOperation(FakeOperation::Kind::Sign);
}

// --- CredentialsAdaptor -----------------------------------------------------
CredentialsAdaptor::CredentialsAdaptor(QObject* parent, FakeAgent* agent, CardAdaptor* cardAdaptor)
    : QDBusAbstractAdaptor(parent), m_agent(agent), m_cardAdaptor(cardAdaptor)
{}

QDBusObjectPath CredentialsAdaptor::sendEntryError(const QString& errorName)
{
    // Mirror CardAdaptor::sendMethodEntryError: the QDBusContext lives on the
    // registered CardObject (our parent), not on the adaptor.
    auto* ctx = qobject_cast<CardObject*>(parent());
    if (ctx && ctx->calledFromDBus()) {
        ctx->setDelayedReply(true);
        ctx->connection().send(ctx->message().createErrorReply(errorName, QStringLiteral("credential entry error")));
    }
    return QDBusObjectPath();
}

bool CredentialsAdaptor::lacksPinManagement() const
{
    // The real agent's capability entry gate (Credentials1.xml): all three
    // methods require Card1.Capabilities bit 3 (PinManagement, value 8); when it
    // is absent they throw UnsupportedOnThisCard at entry and mint NO Operation.
    // Read THIS card's sibling Card1 adaptor's LIVE caps, not the
    // construction-time Config (per-card gating, exactly like the real agent).
    return m_cardAdaptor == nullptr || (m_cardAdaptor->capabilities() & 8u) == 0u;
}

QDBusObjectPath CredentialsAdaptor::ManagePin(const QString& pinId, const QString& verb, const QVariantMap& options)
{
    if (lacksPinManagement()) {
        return sendEntryError(QStringLiteral("org.librescrs.Agent.Error.UnsupportedOnThisCard"));
    }
    if (m_agent->config().credEntryError) {
        return sendEntryError(m_agent->config().credEntryErrorName);
    }
    // Request-side wire vocabulary (agent-side entry validation, before any id
    // resolution — the request shape is judged first, and a refusal never
    // reaches the card, so the listing cache survives):
    //   - verb is the CLOSED set change | unblock | activate_pin;
    //   - options is the CLOSED key set {activateKey: bool}, legal only with
    //     activate_pin (there is no open options container on this wire).
    // Reachability, stated plainly so this is neither trusted as a live guard
    // nor deleted as dead code: nothing in THIS repo reaches these refusals any
    // more. The option branches cannot be constructed through the client's typed
    // entry point at all — its options struct carries a single bool, so there is
    // no key to invent and no value to mistype, and the client withholds that
    // bool unless the verb is activate_pin. The verb branch is reachable only by
    // deliberately marshalling an out-of-range enumerator, which converts to the
    // empty token; the suite that did so is the client library's, which owns the
    // conversion and tests it there. The gate stays anyway, because the fake's
    // job is to refuse precisely what the real agent's entry validation refuses:
    // a permissive double would let a malformed request pass wherever some later
    // client IS exercised against it.
    static const QStringList kVerbs = {QStringLiteral("change"), QStringLiteral("unblock"),
                                       QStringLiteral("activate_pin")};
    if (!kVerbs.contains(verb)) {
        return sendEntryError(QStringLiteral("org.librescrs.Agent.Error.InvalidRequest"));
    }
    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        const bool knownKey = it.key() == QLatin1StringView("activateKey");
        const bool wellTyped = it.value().typeId() == QMetaType::Bool;
        const bool legalHere = verb == QLatin1StringView("activate_pin");
        if (!knownKey || !wellTyped || !legalHere) {
            return sendEntryError(QStringLiteral("org.librescrs.Agent.Error.InvalidRequest"));
        }
    }
    // List-before-mutate (agent-side): an id can only come from
    // a CURRENT listing. No listing yet — or a previous mutation dropped the
    // cache — means the id is stale by definition: refused UnknownCredential
    // until a fresh ListCredentials. A conforming client (mandatory re-list
    // after every mutation) never sees this. An id OUTSIDE the current
    // listing's snapshot is equally unresolvable (ids exist only in listings);
    // the refusal itself does not drop the listing.
    if (!m_agent->hasCurrentListing() || !m_agent->isListedId(pinId)) {
        return sendEntryError(QStringLiteral("org.librescrs.Agent.Error.UnknownCredential"));
    }
    m_agent->invalidateListing(); // the mutation invalidates the listing cache
    // A mutation Result carries only the a{sv} outcome — never records.
    return m_agent->mintOperation(FakeOperation::Kind::Credentials, /*withCredRecords=*/false);
}
QDBusObjectPath CredentialsAdaptor::ActivateSigningKey()
{
    if (lacksPinManagement()) {
        return sendEntryError(QStringLiteral("org.librescrs.Agent.Error.UnsupportedOnThisCard"));
    }
    if (m_agent->config().credEntryError) {
        return sendEntryError(m_agent->config().credEntryErrorName);
    }
    // Id-less: per the XML it can never draw UnknownCredential (a call with no
    // activatable key is a VALID call answered via outcome=unsupported), so no
    // listing gate — but it IS a mutation, so it drops the listing cache.
    m_agent->invalidateListing();
    // A mutation Result carries only the a{sv} outcome — never records.
    return m_agent->mintOperation(FakeOperation::Kind::Credentials, /*withCredRecords=*/false);
}
QDBusObjectPath CredentialsAdaptor::ListCredentials()
{
    if (lacksPinManagement()) {
        return sendEntryError(QStringLiteral("org.librescrs.Agent.Error.UnsupportedOnThisCard"));
    }
    // credEntryError models an id-bearing refusal (UnknownCredential / RateLimited
    // against a specific pinId), so it scopes to the MUTATION methods only —
    // ListCredentials is id-less and cannot draw those, and must stay live so the
    // client's mandatory post-mutation / UnknownCredential recovery re-list works.
    m_agent->noteListingIssued(); // the agent's listing cache is (re)populated
    return m_agent->mintOperation(FakeOperation::Kind::Credentials);
}

// --- FakeAgent -------------------------------------------------------------
FakeAgent::FakeAgent(QDBusConnection connection, Config config, QObject* parent)
    : QObject(parent), m_connection(connection), m_config(std::move(config))
{
    ensureMetatypes();
    exportTree();
}

FakeAgent::~FakeAgent()
{
    if (m_wedgedObject != nullptr) {
        m_connection.unregisterObject(m_wedgedPath);
    }
    m_connection.unregisterObject(m_cardPath);
    m_connection.unregisterObject(m_readerPath);
    m_connection.unregisterObject(m_rootPath);
}

void FakeAgent::exportTree()
{
    // Root object hosts the ObjectManager AND the Pkcs11_1 broker surface
    // (which the real agent also hosts once, on the manager path). It is a
    // CardObject so it carries a QDBusContext the Pkcs11Adaptor uses to mint a
    // CertDer error reply (an adaptor's own context is not populated for plain
    // method calls).
    m_rootObject = new CardObject(this);
    m_objectManager = new ObjectManagerAdaptor(m_rootObject, this);
    m_pkcs11Adaptor = new Pkcs11Adaptor(m_rootObject, this);
    m_connection.registerObject(m_rootPath, m_rootObject);

    // Reader.
    m_readerObject = new QObject(this);
    m_readerAdaptor =
        new ReaderAdaptor(m_readerObject, m_config.readerName, m_config.hasCard, QDBusObjectPath(m_cardPath));
    m_connection.registerObject(m_readerPath, m_readerObject);

    // Card (only when present).
    if (m_config.hasCard) {
        m_cardObject = new CardObject(this);
        m_cardAdaptor = new CardAdaptor(m_cardObject, this, m_config.capabilities, QDBusObjectPath(m_readerPath),
                                        m_config.preReadAuth);
        new CredentialsAdaptor(m_cardObject, this, m_cardAdaptor);
        m_connection.registerObject(m_cardPath, m_cardObject);
    }
}

QDBusConnection FakeAgent::connection() const
{
    return m_connection;
}
QString FakeAgent::service() const
{
    return m_config.service;
}
QString FakeAgent::rootPath() const
{
    return m_rootPath;
}
QString FakeAgent::readerPath() const
{
    return m_readerPath;
}
QString FakeAgent::cardPath() const
{
    return m_cardPath;
}
FakeAgent::Config& FakeAgent::config()
{
    return m_config;
}

QString FakeAgent::wedgedPropertiesPath()
{
    if (m_wedgedObject == nullptr) {
        // A CardObject (QDBusContext) hosts BOTH a Card1 adaptor (so a proxy's
        // PropertiesChanged match wiring is faithful) AND the wedged Properties
        // adaptor — whose GetAll shadows the Card1 auto-export's by being
        // registered explicitly on the same interface name.
        m_wedgedObject = new CardObject(this);
        new CardAdaptor(m_wedgedObject, this, m_config.capabilities, QDBusObjectPath(m_readerPath),
                        m_config.preReadAuth);
        m_wedgedProps = new WedgedPropertiesAdaptor(m_wedgedObject);
        m_connection.registerObject(m_wedgedPath, m_wedgedObject);
    }
    return m_wedgedPath;
}

void FakeAgent::emitWedgedCardInvalidated()
{
    QDBusMessage sig = QDBusMessage::createSignal(m_wedgedPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                  QStringLiteral("PropertiesChanged"));
    sig << QStringLiteral("org.librescrs.Agent.Card1") << QVariantMap{} << QStringList{QStringLiteral("Capabilities")};
    m_connection.send(sig);
}

void FakeAgent::emitWedgedReaderInvalidated()
{
    QDBusMessage sig = QDBusMessage::createSignal(m_wedgedPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                  QStringLiteral("PropertiesChanged"));
    sig << QStringLiteral("org.librescrs.Agent.Reader1") << QVariantMap{} << QStringList{QStringLiteral("Name")};
    m_connection.send(sig);
}

void FakeAgent::emitWedgedCardPropsChanged(const QVariantMap& changed)
{
    QDBusMessage sig = QDBusMessage::createSignal(m_wedgedPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                  QStringLiteral("PropertiesChanged"));
    sig << QStringLiteral("org.librescrs.Agent.Card1") << changed << QStringList{};
    m_connection.send(sig);
}

void FakeAgent::scriptWedgedGetAll(int delayMs, const QVariantMap& props)
{
    (void)wedgedPropertiesPath(); // materialise the wedged object on first use
    m_wedgedProps->scriptGetAllReply(delayMs, props);
}

int FakeAgent::wedgedGetAllCallCount() const
{
    return m_wedgedProps != nullptr ? m_wedgedProps->getAllCallCount() : 0;
}

FakeManagedObjects FakeAgent::managedObjects() const
{
    FakeManagedObjects out;

    FakeInterfaceProps readerIfaces;
    QVariantMap readerProps;
    readerProps.insert(QStringLiteral("Name"), m_readerAdaptor->name());
    readerProps.insert(QStringLiteral("HasCard"), m_readerAdaptor->hasCard());
    readerProps.insert(QStringLiteral("Card"), QVariant::fromValue(m_readerAdaptor->card()));
    readerIfaces.insert(QStringLiteral("org.librescrs.Agent.Reader1"), readerProps);
    out.insert(QDBusObjectPath(m_readerPath), readerIfaces);

    if (m_cardAdaptor) {
        FakeInterfaceProps cardIfaces;
        QVariantMap cardProps;
        cardProps.insert(QStringLiteral("Capabilities"), m_cardAdaptor->capabilities());
        cardProps.insert(QStringLiteral("Reader"), QVariant::fromValue(m_cardAdaptor->reader()));
        cardProps.insert(QStringLiteral("PreReadAuthMethod"), m_cardAdaptor->preReadAuthMethod());
        cardIfaces.insert(QStringLiteral("org.librescrs.Agent.Card1"), cardProps);
        out.insert(QDBusObjectPath(m_cardPath), cardIfaces);
    }

    // A real agent's GetManagedObjects enumerates EVERY exported object, so a
    // second reader (however it was surfaced — announced or registered silently)
    // is part of the snapshot. Include it so a discovery re-run recovers a reader
    // the client's live signal path missed.
    if (m_reader2Adaptor) {
        FakeInterfaceProps reader2Ifaces;
        QVariantMap reader2Props;
        reader2Props.insert(QStringLiteral("Name"), m_reader2Adaptor->name());
        reader2Props.insert(QStringLiteral("HasCard"), m_reader2Adaptor->hasCard());
        reader2Props.insert(QStringLiteral("Card"), QVariant::fromValue(m_reader2Adaptor->card()));
        reader2Ifaces.insert(QStringLiteral("org.librescrs.Agent.Reader1"), reader2Props);
        out.insert(QDBusObjectPath(m_reader2Path), reader2Ifaces);
    }
    if (m_card2Adaptor) {
        FakeInterfaceProps card2Ifaces;
        QVariantMap card2Props;
        card2Props.insert(QStringLiteral("Capabilities"), m_card2Adaptor->capabilities());
        card2Props.insert(QStringLiteral("Reader"), QVariant::fromValue(m_card2Adaptor->reader()));
        card2Props.insert(QStringLiteral("PreReadAuthMethod"), m_card2Adaptor->preReadAuthMethod());
        card2Ifaces.insert(QStringLiteral("org.librescrs.Agent.Card1"), card2Props);
        out.insert(QDBusObjectPath(m_card2Path), card2Ifaces);
    }
    return out;
}

void FakeAgent::captureSign(const QString& certId, const QByteArray& inputBytes, const QVariantMap& options)
{
    ++m_signCallCount;
    m_lastSignCertId = certId;
    m_lastSignInputBytes = inputBytes;
    m_lastSignOptions = options;
}

int FakeAgent::signCallCount() const
{
    return m_signCallCount;
}

QString FakeAgent::lastSignCertId() const
{
    return m_lastSignCertId;
}

QVariantMap FakeAgent::lastSignOptions() const
{
    return m_lastSignOptions;
}

QByteArray FakeAgent::lastSignInputBytes() const
{
    return m_lastSignInputBytes;
}

void FakeAgent::captureCertDer(const QString& reader, const QString& certId)
{
    m_lastCertDerReader = reader;
    m_lastCertDerCertId = certId;
}
QString FakeAgent::lastCertDerReader() const
{
    return m_lastCertDerReader;
}
QString FakeAgent::lastCertDerCertId() const
{
    return m_lastCertDerCertId;
}

QDBusObjectPath FakeAgent::mintOperation(FakeOperation::Kind kind, bool withCredRecords)
{
    const QString opPath = QStringLiteral("/org/librescrs/Agent/op/%1").arg(m_opCounter++);
    const int delay = m_config.raceResultBeforeReturn ? 0 : m_config.operationDelayMs;
    // Photo-specific no-photo modes apply ONLY to the best-effort GetPhoto op, so
    // the preceding ReadIdentity op still succeeds (Ok + Result) and the handler
    // reaches its identity surface before the no-photo path is exercised.
    const bool suppressResult =
        m_config.suppressResult || (kind == FakeOperation::Kind::Photo && m_config.photoSuppressResult);
    const bool photoEmptyMap = (kind == FakeOperation::Kind::Photo) && m_config.photoEmptyMap;
    // A Credentials MUTATION result carries no records (withCredRecords=false):
    // records ride ListCredentials results alone, exactly like the real agent.
    const FakeCredentialRecords credRecords = withCredRecords ? m_config.credRecords : FakeCredentialRecords{};
    auto* op = new FakeOperation(this, m_connection, opPath, kind, delay, m_config.finalStatus, m_config.finalErrorCode,
                                 suppressResult, m_config.certScript, m_config.rawCertResult, m_config.photoBytes,
                                 photoEmptyMap, m_config.announceConsentPhase, m_config.lostSignalRecoverable,
                                 m_config.credResult, credRecords);
    if (!m_config.signMeta.isEmpty()) {
        op->setSignMeta(m_config.signMeta);
    }
    m_operations.append(op);
    // When raceResultBeforeReturn is set, delay is 0 so start() fires Result +
    // Finished synchronously here, BEFORE we return the path — the client
    // subscribes only after it receives the path, so it MUST recover via
    // GetResult / the terminal triple. Otherwise start() arms a QTimer and the
    // signals arrive after the client has subscribed.
    op->start();
    return QDBusObjectPath(opPath);
}

int FakeAgent::operationCount() const
{
    return m_opCounter;
}

void FakeAgent::emitCardCapabilitiesChanged(uint capabilities)
{
    if (!m_cardAdaptor) {
        return;
    }
    m_cardAdaptor->setCapabilities(capabilities);

    QDBusMessage sig = QDBusMessage::createSignal(m_cardPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                  QStringLiteral("PropertiesChanged"));
    QVariantMap changed;
    changed.insert(QStringLiteral("Capabilities"), capabilities);
    sig << QStringLiteral("org.librescrs.Agent.Card1") << changed << QStringList{};
    m_connection.send(sig);
}

void FakeAgent::invalidateCardCapabilities(uint capabilities)
{
    if (!m_cardAdaptor) {
        return;
    }
    // The GetAll fallback will read this value back through the auto-exported
    // Properties interface.
    m_cardAdaptor->setCapabilities(capabilities);

    QDBusMessage sig = QDBusMessage::createSignal(m_cardPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                  QStringLiteral("PropertiesChanged"));
    sig << QStringLiteral("org.librescrs.Agent.Card1") << QVariantMap{} << QStringList{QStringLiteral("Capabilities")};
    m_connection.send(sig);
}

void FakeAgent::setCardCapabilitiesSilently(uint capabilities)
{
    if (!m_cardAdaptor) {
        return;
    }
    // Deliberately NO PropertiesChanged: the client's cached caps stay stale,
    // modelling a client-vs-agent capability desync.
    m_cardAdaptor->setCapabilities(capabilities);
}

int FakeAgent::cancelledOperationCount() const
{
    return m_cancelledOps;
}

void FakeAgent::noteOperationCancelled()
{
    ++m_cancelledOps;
}

bool FakeAgent::hasCurrentListing() const
{
    return m_hasCurrentListing;
}

void FakeAgent::noteListingIssued()
{
    m_hasCurrentListing = true;
    // Snapshot the ids THIS listing returns: ManagePin resolves pinIds against
    // exactly what was last listed, mirroring the real agent's per-listing map.
    m_currentListingIds.clear();
    for (const QVariantMap& record : std::as_const(m_config.credRecords)) {
        const QString id = record.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            m_currentListingIds.append(id);
        }
    }
}

void FakeAgent::invalidateListing()
{
    m_hasCurrentListing = false;
    m_currentListingIds.clear();
}

bool FakeAgent::isListedId(const QString& pinId) const
{
    return m_currentListingIds.contains(pinId);
}

void FakeAgent::reAddCardWithCapabilities(uint capabilities)
{
    if (!m_cardAdaptor) {
        return;
    }
    m_cardAdaptor->setCapabilities(capabilities);

    FakeInterfaceProps cardIfaces;
    QVariantMap cardProps;
    cardProps.insert(QStringLiteral("Capabilities"), capabilities);
    cardProps.insert(QStringLiteral("Reader"), QVariant::fromValue(m_cardAdaptor->reader()));
    cardProps.insert(QStringLiteral("PreReadAuthMethod"), m_cardAdaptor->preReadAuthMethod());
    cardIfaces.insert(QStringLiteral("org.librescrs.Agent.Card1"), cardProps);
    Q_EMIT m_objectManager->InterfacesAdded(QDBusObjectPath(m_cardPath), cardIfaces);
}

void FakeAgent::emitReaderArrivesEmpty()
{
    // (a) A brand-new reader appears, EMPTY: HasCard=false, Card="/". The real
    // agent never announces a reader with HasCard already true.
    m_reader2Object = new QObject(this);
    m_reader2Adaptor = new ReaderAdaptor(m_reader2Object, m_config.reader2Name, /*hasCard=*/false,
                                         QDBusObjectPath(QStringLiteral("/")));
    m_connection.registerObject(m_reader2Path, m_reader2Object);

    FakeInterfaceProps readerIfaces;
    QVariantMap readerProps;
    readerProps.insert(QStringLiteral("Name"), m_reader2Adaptor->name());
    readerProps.insert(QStringLiteral("HasCard"), false);
    readerProps.insert(QStringLiteral("Card"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/"))));
    readerIfaces.insert(QStringLiteral("org.librescrs.Agent.Reader1"), readerProps);
    Q_EMIT m_objectManager->InterfacesAdded(QDBusObjectPath(m_reader2Path), readerIfaces);
}

QString FakeAgent::emitArrivedReaderCardAdded(uint capabilities, const QString& preReadAuth)
{
    // (b) The card object appears under its own path — with the full per-card
    // surface (Card1 + Credentials1), exactly like the real agent's cards.
    m_card2Object = new CardObject(this);
    m_card2Adaptor = new CardAdaptor(m_card2Object, this, capabilities, QDBusObjectPath(m_reader2Path), preReadAuth);
    new CredentialsAdaptor(m_card2Object, this, m_card2Adaptor);
    m_connection.registerObject(m_card2Path, m_card2Object);

    FakeInterfaceProps cardIfaces;
    QVariantMap cardProps;
    cardProps.insert(QStringLiteral("Capabilities"), capabilities);
    cardProps.insert(QStringLiteral("Reader"), QVariant::fromValue(QDBusObjectPath(m_reader2Path)));
    cardProps.insert(QStringLiteral("PreReadAuthMethod"), preReadAuth);
    cardIfaces.insert(QStringLiteral("org.librescrs.Agent.Card1"), cardProps);
    Q_EMIT m_objectManager->InterfacesAdded(QDBusObjectPath(m_card2Path), cardIfaces);
    return m_card2Path;
}

void FakeAgent::emitArrivedReaderHasCard()
{
    // (c) The reader flips HasCard=true AND Card=<cardPath> together, exactly as
    // ReaderObject::updateCardPresence does (both full values in `changed`).
    m_reader2Adaptor->setHasCard(true);
    m_reader2Adaptor->setCard(QDBusObjectPath(m_card2Path));
    QDBusMessage sig = QDBusMessage::createSignal(m_reader2Path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                  QStringLiteral("PropertiesChanged"));
    QVariantMap changed;
    changed.insert(QStringLiteral("HasCard"), true);
    changed.insert(QStringLiteral("Card"), QVariant::fromValue(QDBusObjectPath(m_card2Path)));
    sig << QStringLiteral("org.librescrs.Agent.Reader1") << changed << QStringList{};
    m_connection.send(sig);
}

void FakeAgent::emitReaderHasCardChanged(bool hasCard)
{
    // Mirror the real agent's ReaderObject::updateCardPresence: a card
    // insert/removal flips the owning Reader1's HasCard *and* Card together in
    // a single Properties.PropertiesChanged carrying the full new values in the
    // `changed` map (Card -> the card path on insert, -> "/" on remove).
    QDBusMessage sig = QDBusMessage::createSignal(m_readerPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                  QStringLiteral("PropertiesChanged"));
    QVariantMap changed;
    changed.insert(QStringLiteral("HasCard"), hasCard);
    changed.insert(QStringLiteral("Card"), QVariant::fromValue(m_readerAdaptor->card()));
    sig << QStringLiteral("org.librescrs.Agent.Reader1") << changed << QStringList{};
    m_connection.send(sig);
}

void FakeAgent::setCardPresent(bool present)
{
    // A removal/insertion is a new card session: the listing cache never
    // survives it (the real agent's cache is per card session).
    invalidateListing();
    if (present && !m_cardAdaptor) {
        m_cardObject = new CardObject(this);
        m_cardAdaptor = new CardAdaptor(m_cardObject, this, m_config.capabilities, QDBusObjectPath(m_readerPath),
                                        m_config.preReadAuth);
        new CredentialsAdaptor(m_cardObject, this, m_cardAdaptor); // a re-inserted card keeps its Credentials1 surface
        m_connection.registerObject(m_cardPath, m_cardObject);
        m_readerAdaptor->setHasCard(true);

        FakeInterfaceProps cardIfaces;
        QVariantMap cardProps;
        cardProps.insert(QStringLiteral("Capabilities"), m_cardAdaptor->capabilities());
        cardProps.insert(QStringLiteral("Reader"), QVariant::fromValue(m_cardAdaptor->reader()));
        cardProps.insert(QStringLiteral("PreReadAuthMethod"), m_cardAdaptor->preReadAuthMethod());
        cardIfaces.insert(QStringLiteral("org.librescrs.Agent.Card1"), cardProps);
        Q_EMIT m_objectManager->InterfacesAdded(QDBusObjectPath(m_cardPath), cardIfaces);
        m_readerAdaptor->setCard(QDBusObjectPath(m_cardPath));
        emitReaderHasCardChanged(true);
    } else if (!present && m_cardAdaptor) {
        m_connection.unregisterObject(m_cardPath);
        m_readerAdaptor->setHasCard(false);
        Q_EMIT m_objectManager->InterfacesRemoved(QDBusObjectPath(m_cardPath),
                                                  {QStringLiteral("org.librescrs.Agent.Card1")});
        m_readerAdaptor->setCard(QDBusObjectPath(QStringLiteral("/")));
        emitReaderHasCardChanged(false);
        m_cardObject->deleteLater();
        m_cardObject = nullptr;
        m_cardAdaptor = nullptr;
    }
}

void FakeAgent::registerSecondReaderSilently()
{
    // A second reader hot-plugged, but its InterfacesAdded is DROPPED (the live
    // signal the client's discovery path missed on hardware). The Reader1 object
    // IS registered so GetManagedObjects returns it — a manual refresh recovers
    // it. Present with no card, like a freshly-plugged empty reader.
    if (m_reader2Adaptor) {
        return;
    }
    m_reader2Object = new QObject(this);
    m_reader2Adaptor = new ReaderAdaptor(m_reader2Object, m_config.reader2Name, /*hasCard=*/false,
                                         QDBusObjectPath(QStringLiteral("/")));
    m_connection.registerObject(m_reader2Path, m_reader2Object);
    // Deliberately NO ObjectManager InterfacesAdded emission.
}

void FakeAgent::exportCardSilently()
{
    // Deferred-publish window / dropped InterfacesAdded: the Card1 object exists
    // on the tree (so GetManagedObjects returns it) and the owning Reader1
    // reports HasCard=true / Card=<path>, but the Card1 InterfacesAdded is NOT
    // emitted. A client that discovered this reader while it was empty therefore
    // sees a card it cannot resolve until it re-runs discovery.
    if (m_cardAdaptor) {
        return; // precondition: no card yet (construct with hasCard=false)
    }
    m_cardObject = new CardObject(this);
    m_cardAdaptor =
        new CardAdaptor(m_cardObject, this, m_config.capabilities, QDBusObjectPath(m_readerPath), m_config.preReadAuth);
    new CredentialsAdaptor(m_cardObject, this, m_cardAdaptor);
    m_connection.registerObject(m_cardPath, m_cardObject);
    m_readerAdaptor->setHasCard(true);
    m_readerAdaptor->setCard(QDBusObjectPath(m_cardPath));
    // Reader1 PropertiesChanged (HasCard + Card) fires — but NO Card1
    // InterfacesAdded. This is the whole point: the reader claims a card the
    // client has not been told how to reach.
    emitReaderHasCardChanged(true);
}

void FakeAgent::dropCardSilently()
{
    if (!m_cardAdaptor) {
        return;
    }
    // Unregister the Card1 object (GetManagedObjects stops returning it) and flip
    // the reader to card-less — but emit NO InterfacesRemoved and NO Reader1
    // PropertiesChanged. The client keeps its now-stale card until a discovery
    // re-run reconciles it away.
    m_connection.unregisterObject(m_cardPath);
    m_readerAdaptor->setHasCard(false);
    m_readerAdaptor->setCard(QDBusObjectPath(QStringLiteral("/")));
    m_cardObject->deleteLater();
    m_cardObject = nullptr;
    m_cardAdaptor = nullptr;
}

// --- reference-out-parameter introspection ---------------------------------
// Lives at the bottom of this file because half these classes are declared in
// it, so this is the first point where all twelve are complete types.
QList<const QMetaObject*> adaptorMetaObjects()
{
    return {
        &WedgedPropertiesAdaptor::staticMetaObject, &ObjectManagerAdaptor::staticMetaObject,
        &ReaderAdaptor::staticMetaObject,           &CardAdaptor::staticMetaObject,
        &CredentialsAdaptor::staticMetaObject,      &Pkcs11Adaptor::staticMetaObject,
        &FakeOperationAdaptor::staticMetaObject,    &FakeSignAdaptor::staticMetaObject,
        &FakeIdentityAdaptor::staticMetaObject,     &FakePhotoAdaptor::staticMetaObject,
        &FakeCertificatesAdaptor::staticMetaObject, &FakeCredentialsAdaptor::staticMetaObject,
    };
}

QList<QByteArray> adaptorReferenceOutParameterTypes()
{
    QList<QByteArray> out;
    for (const QMetaObject* mo : adaptorMetaObjects()) {
        // From methodOffset(): the adaptor's OWN methods, skipping the ones
        // QObject and QDBusAbstractAdaptor contribute.
        for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
            const QList<QByteArray> params = mo->method(i).parameterTypes();
            for (const QByteArray& type : params) {
                // A surviving `&` means moc did not normalize it away, which it
                // does for every `const T&` — so this is an output parameter.
                if (type.endsWith('&')) {
                    out.append(type);
                }
            }
        }
    }
    return out;
}

} // namespace LibreKDETest

#include "FakeAgent.moc"
