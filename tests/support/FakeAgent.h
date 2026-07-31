// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusContext>
#include <QByteArray>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <memory>

/// @file
/// @brief A real `org.librescrs.Agent` peer on a private session bus, for
///        exercising an agent client end-to-end. Mirrors the agent's wire
///        surface (ObjectManager + Reader1 + Card1 + a scripted Operation1
///        with a typed Sign1/Identity1 result), but is driven entirely by test
///        scripting hooks — no PC/SC, no real card. It depends on no client
///        header, so it is not tied to one client: see the wire-shape mirror
///        note below.
///
/// Run the host process under `dbus-run-session` so the bus is isolated, per
/// the agent's own DBusServiceTest harness.

namespace LibreKDETest {

// --- wire-shape mirrors ----------------------------------------------------
//
// Every payload type the fake marshals is declared HERE, in the fake's own
// namespace, instead of being borrowed from a client's headers. A client keeps
// its demarshalling types where its own transport can reach them, which for one
// of the agent clients is a private header that is never installed — a peer
// cannot include it. So the fake carries its own mirrors.
//
// This works because QtDBus routes a payload to a slot by SIGNATURE STRING, not
// by C++ type: a mirror declared member-for-member like the frozen interface XML
// is served to, and demarshaled by, ANY client whose own type has the same
// signature — whatever that type is called or where it lives.
//
// The flip side is the reason these are pinned by a test: a mirror whose members
// drift still COMPILES. It fails at run time, when a payload arrives with a
// signature no client slot matches, in whichever suite happens to exercise that
// one interface. FakeAgentWireShapeTest asserts each registered signature string
// against a hand-transcribed literal so a drift fails at test time instead. That
// is a pin, not a build edge — this repo has no edge to the agent's published
// interface XML, so the literals cannot auto-follow a change made there; see the
// note at the top of that test for what the pin does and does not buy.

/// @brief One Identity1 field — the `(sssv)` tuple of `a{sa{s(sssv)}}`.
struct FakeIdentityField
{
    QString labelKey;
    QString labelFallback;
    QString type;       ///< "text" | "date" | "binary"
    QDBusVariant value; ///< "s" for text/date, "ay" for binary
};

/// @brief Identity1 field map: group → (field → field-tuple) — the
///        `a{s(sssv)}` group and the `a{sa{s(sssv)}}` Result payload.
using FakeIdentityFieldGroup = QMap<QString, FakeIdentityField>;
using FakeIdentityFields = QMap<QString, FakeIdentityFieldGroup>;

QDBusArgument& operator<<(QDBusArgument& arg, const FakeIdentityField& f);
const QDBusArgument& operator>>(const QDBusArgument& arg, FakeIdentityField& f);

/// @brief One Certificates1 field — the `(ssv)` tuple (labelKey, labelFallback,
///        value) of the `a{sa{s(ssv)}}` field-group map. Deliberately one member
///        SHORTER than Identity1's `(sssv)`: Certificates1 carries no redundant
///        type string. The intermediate group types are registered too, so Qt
///        derives the nested container signature in both directions rather than
///        the fake hand-rolling `beginMap` signatures.
struct FakeCertField
{
    QString labelKey;
    QString labelFallback;
    QDBusVariant value; ///< "s" UTF-8 string inside a variant `v`, as the agent emits it
};
using FakeCertFieldGroup = QMap<QString, FakeCertField>;
using FakeCertFieldGroups = QMap<QString, FakeCertFieldGroup>;

QDBusArgument& operator<<(QDBusArgument& arg, const FakeCertField& f);
const QDBusArgument& operator>>(const QDBusArgument& arg, FakeCertField& f);

/// @brief One Certificates1 entry — the wire struct `(s b a{sa{s(ssv)}} u as as u)`.
///        Seven members go on the wire; the struct below declares nine, because
///        the three display strings are NOT wire members. The marshaller folds
///        them into the `fields` map (`a{sa{s(ssv)}}`, the third wire member)
///        under the group keys the agent uses, so adding another display string
///        is a new dict entry and leaves the signature alone.
struct FakeCertInfo
{
    QString certId;                   ///< opaque SHA-256(DER) handle; the value Sign() takes
    bool signingCapable = false;      ///< paired on-card key + signing-suitable keyUsage
    QString subjectCn;                ///< subject/cn field group (display only)
    QString issuerCn;                 ///< issuer/cn field group (display only)
    QString notAfter;                 ///< validity/notAfter field group (display only)
    quint32 keyUsageBits = 0;         ///< `u` X.509 KeyUsage bitmask
    QStringList extendedKeyUsageOids; ///< `as` EKU OIDs (dotted)
    QStringList chainSubjectCns;      ///< `as` ordered leaf..root subject CNs
    quint32 trustStatus = 255;        ///< `u` trust verdict (255 = Unknown)
};
using FakeCertInfoList = QList<FakeCertInfo>;

QDBusArgument& operator<<(QDBusArgument& arg, const FakeCertInfo& c);
const QDBusArgument& operator>>(const QDBusArgument& arg, FakeCertInfo& c);

// The remaining shapes are plain Qt containers, so mirroring them costs a
// typedef and nothing else. Three of the four take no Q_DECLARE_METATYPE:
// FakeInterfaceProps, FakePhotoMap and FakeCredentialRecords are each literally
// the same C++ type an agent client's public header already declares one for,
// and a second declaration of one type in one translation unit does not compile.
// Qt 6 does not need the macro — QMetaType::fromType<T>() derives the metatype
// either way — but it is what files a type under the NAME it is spelled with,
// and QtDBus resolves a slot's reference OUTPUT parameter by exactly that name
// lookup. FakeCredentialRecords is the one shape used that way, so
// ensureMetatypes() registers its name explicitly instead.

/// @brief ObjectManager `a{sa{sv}}` interface → properties map.
using FakeInterfaceProps = QMap<QString, QVariantMap>;

/// @brief ObjectManager `a{oa{sa{sv}}}` GetManagedObjects map.
using FakeManagedObjects = QMap<QDBusObjectPath, FakeInterfaceProps>;

/// @brief Photo1 `a{sh}` result: `"groupKey:fieldKey"` → sealed memfd.
using FakePhotoMap = QMap<QString, QDBusUnixFileDescriptor>;

/// @brief Operation.Credentials1 `aa{sv}` records (empty for a mutation).
using FakeCredentialRecords = QList<QVariantMap>;

class FakeAgent;

/// @brief A custom `org.freedesktop.DBus.Properties` adaptor whose `GetAll`
///        deliberately WEDGES by default — it marks the call a delayed reply
///        and never answers. Exercises a hung agent: the client's capped GetAll
///        must time out at kPropTimeoutMs instead of stalling forever. Hosted
///        on its own object path (NOT the card/reader, whose auto-exported
///        Properties interface must keep working for the rest of the suite).
///        `scriptGetAllReply()` turns the wedge into a SLOW agent instead: the
///        call is answered after a scripted delay with a scripted props map,
///        for the async-refresh responsiveness and stale-reply-ordering tests.
///        MUST be parented to a `CardObject`: the QDBusContext used to wedge /
///        delay lives on the registered object, never on the adaptor itself (an
///        adaptor's own context is not populated for plain method calls).
class WedgedPropertiesAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.Properties")
public:
    explicit WedgedPropertiesAdaptor(QObject* parent) : QDBusAbstractAdaptor(parent) {}

    /// @brief Stop wedging: answer each GetAll after @p delayMs with @p props
    ///        (0 = reply on the next server event-loop turn).
    void scriptGetAllReply(int delayMs, const QVariantMap& props);

    /// @brief How many GetAll calls arrived so far (wedged and scripted alike),
    ///        so a test can assert a recovery re-fetch was actually issued.
    [[nodiscard]] int getAllCallCount() const;

public Q_SLOTS:
    QVariantMap GetAll(const QString& iface);

Q_SIGNALS:
    void PropertiesChanged(const QString& iface, const QVariantMap& changed, const QStringList& invalidated);

private:
    int m_replyDelayMs = -1;     ///< < 0 = wedge forever (default)
    QVariantMap m_scriptedProps; ///< the a{sv} payload a scripted reply carries
    int m_getAllCalls = 0;       ///< arrived GetAll calls (answered or wedged)
};

/// @brief ObjectManager adaptor on the root object.
class ObjectManagerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")
public:
    ObjectManagerAdaptor(QObject* parent, FakeAgent* agent);

public Q_SLOTS:
    FakeManagedObjects GetManagedObjects();

Q_SIGNALS:
    void InterfacesAdded(const QDBusObjectPath& objectPath, const FakeInterfaceProps& interfacesAndProperties);
    void InterfacesRemoved(const QDBusObjectPath& objectPath, const QStringList& interfaces);

private:
    FakeAgent* m_agent;
};

/// @brief Reader1 adaptor (Name / HasCard / Card properties).
class ReaderAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.librescrs.Agent.Reader1")
    Q_PROPERTY(QString Name READ name)
    Q_PROPERTY(bool HasCard READ hasCard)
    Q_PROPERTY(QDBusObjectPath Card READ card)
public:
    ReaderAdaptor(QObject* parent, QString name, bool hasCard, QDBusObjectPath card);
    [[nodiscard]] QString name() const;
    [[nodiscard]] bool hasCard() const;
    [[nodiscard]] QDBusObjectPath card() const;
    void setHasCard(bool v);
    void setCard(QDBusObjectPath v);

private:
    QString m_name;
    bool m_hasCard;
    QDBusObjectPath m_card;
};

/// @brief The QObject registered at the card path. Inherits QDBusContext so the
///        CardAdaptor's method slots can mint a method-entry error reply (the
///        agent's UnsupportedOnThisCard etc.) WITHOUT creating an Operation —
///        the context lives on the REGISTERED object, not on the adaptor.
class CardObject : public QObject, public QDBusContext
{
    Q_OBJECT
public:
    explicit CardObject(QObject* parent = nullptr) : QObject(parent) {}
};

/// @brief Card1 adaptor; ReadIdentity / Sign mint a scripted Operation1. On the
///        failMethodEntry path it errors at method entry (no Operation), driving
///        the client's CapabilityMissing (nullptr) path.
class CardAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.librescrs.Agent.Card1")
    Q_PROPERTY(uint Capabilities READ capabilities)
    Q_PROPERTY(QDBusObjectPath Reader READ reader)
    Q_PROPERTY(QString PreReadAuthMethod READ preReadAuthMethod)
public:
    CardAdaptor(QObject* parent, FakeAgent* agent, uint capabilities, QDBusObjectPath reader, QString preReadAuth);
    [[nodiscard]] uint capabilities() const;
    [[nodiscard]] QDBusObjectPath reader() const;
    [[nodiscard]] QString preReadAuthMethod() const;
    void setCapabilities(uint v);
    void setPreReadAuthMethod(QString v);

public Q_SLOTS:
    QDBusObjectPath ReadIdentity();
    QDBusObjectPath GetPhoto();
    QDBusObjectPath ReadCertificates();
    QDBusObjectPath Sign(const QString& certId, const QDBusUnixFileDescriptor& inputFd, const QVariantMap& options);

private:
    /// @brief Send the agent's method-entry error (no Operation minted) for the
    ///        active call, returning a sentinel path the client discards.
    QDBusObjectPath sendMethodEntryError();

    FakeAgent* m_agent;
    uint m_capabilities;
    QDBusObjectPath m_reader;
    QString m_preReadAuth;
};

/// @brief Credentials1 adaptor; parented to the same `CardObject` as
///        `CardAdaptor` (so its method-entry error reply is minted through the
///        registered object's `QDBusContext`, mirroring
///        `CardAdaptor::sendMethodEntryError` — an adaptor's own context is not
///        populated for plain method calls). `ManagePin`/`ActivateSigningKey`/
///        `ListCredentials` each mint a scripted `FakeOperation::Kind::Credentials`
///        operation; on `Config::credEntryError` the two MUTATIONS
///        (`ManagePin`/`ActivateSigningKey`) instead send the scripted error name
///        at method entry (no Operation minted). `ListCredentials` stays exempt
///        from the scripted error, so a client's recovery / mandatory re-list
///        still succeeds.
///
///        The fake ENFORCES the real agent's entry contract:
///        - capability gate: without Card1 bit 3 (PinManagement) all three
///          methods throw UnsupportedOnThisCard (no Operation);
///        - request vocabulary: `ManagePin`'s verb must be one of
///          change | unblock | activate_pin, and its options map is the CLOSED
///          key set {activateKey: bool}, legal only with activate_pin — any
///          other verb, key, type or combination is refused InvalidRequest, so
///          a client wire-vocabulary regression fails the suite. A vocabulary
///          refusal never reaches the card and does not drop the listing;
///        - list-before-mutate: `ManagePin` without a current listing — never
///          listed, or invalidated by a previous mutation — is refused
///          UnknownCredential, as is a pinId absent from the CURRENT listing
///          snapshot (ids resolve only against what was last listed); both
///          mutations drop the listing cache, so a client that skips the
///          mandatory re-list fails loudly.
class CredentialsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.librescrs.Agent.Credentials1")
public:
    /// @p cardAdaptor is the sibling Card1 adaptor of the SAME card object: the
    /// capability entry gate reads that card's LIVE Capabilities (each card is
    /// gated on its own bits, exactly like the real agent).
    CredentialsAdaptor(QObject* parent, FakeAgent* agent, CardAdaptor* cardAdaptor);

public Q_SLOTS:
    QDBusObjectPath ManagePin(const QString& pinId, const QString& verb, const QVariantMap& options);
    QDBusObjectPath ActivateSigningKey();
    QDBusObjectPath ListCredentials();

private:
    /// @brief Send @p errorName at method entry (no Operation minted), mirroring
    ///        `CardAdaptor::sendMethodEntryError`.
    QDBusObjectPath sendEntryError(const QString& errorName);
    /// @brief The real agent's capability entry gate: true when THIS card's
    ///        LIVE Capabilities lack bit 3 (PinManagement) — all three methods
    ///        then throw UnsupportedOnThisCard.
    [[nodiscard]] bool lacksPinManagement() const;

    FakeAgent* m_agent;
    CardAdaptor* m_cardAdaptor;
};

/// @brief Pkcs11_1 adaptor, hosted on the manager (root) path. Only `CertDer`
///        is modelled (the one client surface the agentclient calls): returns
///        the scripted DER bytes, or a `…Error.KeyNotFound` D-Bus error when the
///        config requests it. The error reply is minted through the registered
///        root object's QDBusContext (a `CardObject`), mirroring CardAdaptor —
///        an adaptor's own QDBusContext is not populated for plain method calls.
class Pkcs11Adaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.librescrs.Agent.Pkcs11_1")
public:
    Pkcs11Adaptor(QObject* parent, FakeAgent* agent);

public Q_SLOTS:
    QByteArray CertDer(const QDBusObjectPath& reader, const QString& certId);

private:
    FakeAgent* m_agent;
};

/// @brief A scripted Operation1 + its typed result interface. Emits its typed
///        Result then Finished after `delayMs` (0 = synchronous before the
///        method even returns, to exercise the lost-Finished race). Holds the
///        last Sign artifact for `GetResult()` recovery.
/// @brief One scripted certificate the FakeAgent's Certificates1.Result emits.
///        Carries the full real-wire field-set so the cert Result can be
///        hand-marshalled into the raw (sba{sa{s(ssv)}}uasasu) struct.
struct FakeCert
{
    QString certId;
    bool signingCapable = false;
    QString subjectCn;
    QString issuerCn = {};                 ///< issuer/cn field group (display only)
    QString notAfter = {};                 ///< validity/notAfter field group (display only)
    uint keyUsageBits = 0;                 ///< u keyUsageBits
    QStringList extendedKeyUsageOids = {}; ///< as EKU OIDs
    QStringList chainSubjectCns = {};      ///< as ordered leaf..root CN chain
    uint trustStatus = 255;                ///< u trustStatus (255 = Unknown)
};
using FakeCertList = QList<FakeCert>;

class FakeOperation : public QObject, protected QDBusContext
{
    Q_OBJECT
public:
    // Credentials covers ListCredentials AND the ManagePin/ActivateSigningKey
    // mutations: the op's typed Operation.Credentials1 Result carries the
    // scripted (credResult, credRecords) payload.
    enum class Kind { Sign, Identity, Certificates, Photo, Credentials };

    FakeOperation(QObject* parent, QDBusConnection connection, QString path, Kind kind, int delayMs, uint finalStatus,
                  uint finalErrorCode, bool suppressResult, FakeCertList certScript = {}, bool rawCertResult = false,
                  QByteArray photoBytes = {}, bool photoEmptyMap = false, bool announceConsentPhase = false,
                  bool lostSignalRecoverable = false, QVariantMap credResult = {},
                  FakeCredentialRecords credRecords = {});
    ~FakeOperation() override;

    [[nodiscard]] QString path() const;
    void start();

    /// @brief Send the frozen NoResult D-Bus error for the active GetResult call
    ///        (mirrors the real Sign1 contract; sets setDelayedReply so the
    ///        adaptor's return value is discarded).
    void replyNoResult();

private:
    friend class FakeOperationAdaptor;
    friend class FakeSignAdaptor;
    friend class FakeIdentityAdaptor;
    friend class FakeCertificatesAdaptor;
    friend class FakePhotoAdaptor;
    friend class FakeCredentialsAdaptor;
    void fire();
    /// @brief Build the deterministic Identity1 field map ("personal:given_name"
    ///        = "Ana") the Result signal AND Identity1.GetResult both serve, so
    ///        the recovery pull re-serves exactly what a live signal would.
    [[nodiscard]] FakeIdentityFields buildIdentityFields() const;
    /// @brief Build the cert list from m_certScript, shared by the
    ///        Certificates1.Result emit and Certificates1.GetResult recovery.
    [[nodiscard]] FakeCertInfoList buildCertificateList() const;
    /// @brief Seal m_photoBytes into m_keptPhotoFd (idempotent). Called on the
    ///        Ok-result RETAIN path — whether or not the Result signal fires —
    ///        so Photo1.GetResult can re-dup a sealed memfd of the same bytes.
    void retainPhotoFd();
    /// @brief Emit an Operation1 Properties.PropertiesChanged carrying Phase=@p phase
    ///        (so the client's AgentOperation raises phaseChanged) — models the agent
    ///        announcing e.g. AwaitingConsent while a human is at the prompter.
    void emitPhase(uint phase);
    /// @brief Emit a Photo1.Result. Normally a one-entry `a{sh}` photo map (key
    ///        "personal:photo", value a sealed memfd holding m_photoBytes); when
    ///        m_photoEmptyMap is set, emits a genuinely EMPTY map (no entries) to
    ///        exercise the consumer's empty-map guard rather than the empty-fd one.
    void emitPhotoResult();
    /// @brief Hand-marshal the cert script into the raw a(sba{sa{s(ssv)}}uasasu)
    ///        struct and send it as a Certificates1.Result signal, bypassing the
    ///        client's operator<< so the client's operator>> is tested against a
    ///        payload it did NOT itself produce.
    void emitRawCertResult();

    QDBusConnection m_connection;
    QString m_path;
    Kind m_kind;
    int m_delayMs;
    uint m_finalStatus;
    uint m_finalErrorCode;
    bool m_suppressResult; ///< finish Ok WITHOUT emitting the typed Result AND with nothing to recover (total loss)
    bool m_completed = false;
    bool m_lostSignalRecoverable = false; ///< finish Ok, SUPPRESS the Result signal, but RETAIN the payload so
                                          ///< GetResult recovers it (the deterministic lost-signal race)
    bool m_resultRetained = false;        ///< an Ok result was retained -> GetResult serves it (else NoResult)
    int m_keptArtifactFd = -1;            // for Sign GetResult recovery (owned)
    FakeCertList m_certScript;            // for Certificates1.Result
    bool m_rawCertResult = false;         // emit cert Result via a hand-marshalled raw signal
    QByteArray m_photoBytes;              // bytes the Photo1.Result sealed memfd carries
    bool m_photoEmptyMap = false;         // emit an EMPTY a{sh} (no entries) instead of one "personal:photo" entry
    bool m_announceConsentPhase = false;  // emit Phase=AwaitingConsent after a short delay, before finishing
    int m_keptPhotoFd = -1;               // sealed photo memfd, kept alive past the signal send (owned)
    QVariantMap m_credResult;             // a{sv} mutation result the Operation.Credentials1.Result carries
    FakeCredentialRecords m_credRecords;  // aa{sv} records (empty for a mutation)
    std::unique_ptr<class FakeOperationAdaptor> m_opAdaptor;
    std::unique_ptr<QObject> m_resultAdaptor;
};

/// @brief Top-level fake agent. Constructs the tree on a caller-supplied
///        connection (a private `dbus-run-session` server connection) under
///        a caller-supplied service name; test hooks mutate state + script ops.
class FakeAgent : public QObject
{
    Q_OBJECT
public:
    struct Config
    {
        QString service;
        /// Reader1.Name of the initial reader (reader/0). Overridable so tests
        /// can model realistic PC/SC name shapes (serial groups, enumeration
        /// indices, dual-interface siblings) for bound-reader matching.
        QString readerName = QStringLiteral("Fake");
        /// Reader1.Name of the second reader (reader/1) the arrival helpers
        /// (`emitReaderArrivesEmpty`, `registerSecondReaderSilently`) register.
        QString reader2Name = QStringLiteral("Fake2");
        uint capabilities = 0;
        bool hasCard = true;
        QString preReadAuth = QStringLiteral("None");
        int operationDelayMs = 5;            ///< delay before an op fires its result/finished
        uint finalStatus = 0;                ///< 0 Ok / 1 Cancelled / 2 Error
        uint finalErrorCode = 0;             ///< Finished errorCode when status==Error
        bool raceResultBeforeReturn = false; ///< fire op synchronously (delay ignored) before method returns
        bool suppressResult = false;         ///< finish Ok WITHOUT emitting the typed Result AND unrecoverable
                                     ///< (GetResult -> NoResult): the "GetResult also unavailable" negative case
        bool lostSignalRecoverable = false; ///< finish Ok, SUPPRESS the Result signal, but RETAIN the payload so
                                            ///< GetResult recovers it — the DETERMINISTIC lost-signal race (all kinds)
        FakeCertList certScript;            ///< certs ReadCertificates emits on its Certificates1.Result
        bool failMethodEntry =
            false; ///< ReadIdentity/GetPhoto/ReadCertificates/Sign send a D-Bus error at entry (no Operation minted)
        bool rawCertResult = false; ///< emit the cert Result as a hand-marshalled raw signal (bypasses operator<<)
        QByteArray photoBytes;      ///< bytes the GetPhoto Photo1.Result sealed memfd carries ("personal:photo")
        bool photoEmptyMap =
            false; ///< Photo op emits a genuinely EMPTY a{sh} (no entries) — exercises the empty-map guard
        bool photoSuppressResult = false; ///< Photo op finishes Ok WITHOUT emitting any Result (lost-Result race)
        QByteArray certDerBytes;          ///< DER bytes Pkcs11_1.CertDer returns on success
        bool certDerKeyNotFound = false;  ///< CertDer sends …Error.KeyNotFound instead of returning bytes
        bool wedgeGetManagedObjects =
            false; ///< ObjectManager.GetManagedObjects never replies (models the discovery-path hang)
        bool announceConsentPhase =
            false; ///< the op emits an Operation1 Phase=AwaitingConsent (a human at the prompter) before finishing
        bool credEntryError = false; ///< the id-bearing ManagePin/ActivateSigningKey send a D-Bus error at entry
                                     ///< (no Operation minted); ListCredentials is id-less and stays exempt
        QString credEntryErrorName =
            QStringLiteral("org.librescrs.Agent.Error.UnknownCredential"); ///< the error name credEntryError sends
        QVariantMap credResult; ///< a{sv} mutation result the minted Operation.Credentials1.Result carries (and
                                ///< GetResult re-serves) — delivered for EVERY completed attempt, Ok or Error
        FakeCredentialRecords
            credRecords; ///< aa{sv} records a ListCredentials op returns; empty for a mutation (a legitimate result)
    };

    FakeAgent(QDBusConnection connection, Config config, QObject* parent = nullptr);
    ~FakeAgent() override;

    [[nodiscard]] QDBusConnection connection() const;
    [[nodiscard]] QString service() const;
    [[nodiscard]] QString rootPath() const;
    [[nodiscard]] QString readerPath() const;
    [[nodiscard]] QString cardPath() const;

    /// @brief Object path hosting the WedgedPropertiesAdaptor (a Properties
    ///        interface whose GetAll never replies). Materialised on first call;
    ///        a test points an AgentCard/AgentReader here to exercise the capped
    ///        GetAll timeout. The Card1/Reader1 interface is also exported here so
    ///        the proxy's match-rule wiring is faithful.
    [[nodiscard]] QString wedgedPropertiesPath();

    /// @brief Emit a Card1 PropertiesChanged on the wedged path marking
    ///        Capabilities `invalidated` (empty `changed`), forcing the proxy onto
    ///        its GetAll fallback — which then hangs on the wedge and must time
    ///        out at kPropTimeoutMs.
    void emitWedgedCardInvalidated();

    /// @brief Emit a Reader1 PropertiesChanged on the wedged path marking Name
    ///        `invalidated`, forcing an AgentReader proxy onto its GetAll
    ///        fallback against the wedged/scripted Properties adaptor.
    void emitWedgedReaderInvalidated();

    /// @brief Emit a Card1 PropertiesChanged on the wedged path carrying the
    ///        given FULL new values in `changed` (the direct-apply path), so a
    ///        test can race a fresh direct apply against a slow GetAll.
    void emitWedgedCardPropsChanged(const QVariantMap& changed);

    /// @brief Script the wedged path's GetAll to answer after @p delayMs with
    ///        @p props instead of wedging forever (materialises the wedged
    ///        object if needed).
    void scriptWedgedGetAll(int delayMs, const QVariantMap& props);

    /// @brief How many GetAll calls reached the wedged path so far (answered
    ///        or wedged), so a test can assert a recovery re-fetch was issued.
    [[nodiscard]] int wedgedGetAllCallCount() const;

    [[nodiscard]] FakeManagedObjects managedObjects() const;
    [[nodiscard]] Config& config();

    /// @brief Total Operation1 objects minted so far (ReadIdentity/GetPhoto/
    ///        ReadCertificates/Sign). The lazy-card-I/O probe: classification +
    ///        discovery mint ZERO ops, so a non-zero count means a content read.
    [[nodiscard]] int operationCount() const;

    /// @brief Mint a scripted operation object, returning its path. A
    ///        Credentials MUTATION passes @p withCredRecords = false: the real
    ///        agent's mutation Result carries only the a{sv} outcome — records
    ///        ride ListCredentials results alone.
    QDBusObjectPath mintOperation(FakeOperation::Kind kind, bool withCredRecords = true);

    /// @brief Capture the verbatim Sign() in-args (CardAdaptor::Sign dups +
    ///        reads inputFd synchronously, before the client closes its fd).
    void captureSign(const QString& certId, const QByteArray& inputBytes, const QVariantMap& options);

    /// @brief Last Sign() certId / options / input-document bytes, as the fake
    ///        actually received them on the wire.
    [[nodiscard]] QString lastSignCertId() const;
    [[nodiscard]] QVariantMap lastSignOptions() const;
    [[nodiscard]] QByteArray lastSignInputBytes() const;

    /// @brief Record + read the (reader, certId) the last Pkcs11_1.CertDer call
    ///        carried, so a test can assert the client addressed the right card.
    void captureCertDer(const QString& reader, const QString& certId);
    [[nodiscard]] QString lastCertDerReader() const;
    [[nodiscard]] QString lastCertDerCertId() const;

    /// @brief Insert/remove the card live (emits InterfacesAdded/Removed plus
    ///        a Reader1 HasCard PropertiesChanged, mirroring the real agent).
    void setCardPresent(bool present);

    /// @brief Model the agent's deferred-publish window (or a dropped
    ///        `InterfacesAdded`): register the `Card1` object so it appears in
    ///        `GetManagedObjects`, flip the owning Reader1 to HasCard=true /
    ///        Card=<cardPath> and emit that Reader1 `PropertiesChanged` — but do
    ///        NOT emit the `Card1` `InterfacesAdded`. A client thus sees the
    ///        reader claim a card it cannot resolve until it re-runs discovery.
    ///        Precondition: no card is present yet (construct with
    ///        `Config::hasCard = false`).
    void exportCardSilently();

    /// @brief The inverse of exportCardSilently: unregister the `Card1` object so
    ///        `GetManagedObjects` stops returning it and flip the owning `Reader1`
    ///        to HasCard=false / Card="/", but emit NEITHER `InterfacesRemoved` NOR
    ///        the `Reader1` `PropertiesChanged`. Models a DROPPED removal signal —
    ///        the client still tracks a card the agent no longer exports, so only a
    ///        discovery re-run (reconcile) can drop it.
    void dropCardSilently();

    /// @brief Register a SECOND reader (present, empty) so `GetManagedObjects`
    ///        returns it, but DON'T emit its `InterfacesAdded` — the client's
    ///        live roster misses it (the hot-plugged-reader signal drop) until a
    ///        manual discovery re-run picks it up.
    void registerSecondReaderSilently();

    /// @brief Emit a Reader1 PropertiesChanged flipping HasCard *and* Card
    ///        together (both full values in the `changed` map), mirroring the
    ///        real agent's ReaderObject::updateCardPresence. Reads the current
    ///        Card path off the reader adaptor, so callers must set it first.
    void emitReaderHasCardChanged(bool hasCard);

    /// @brief Change the card's Capabilities and emit a Properties.PropertiesChanged
    ///        carrying the FULL new value in the `changed` map (mirrors the
    ///        agent's PresenceModel, which never relies on `invalidated`). Lets
    ///        a test assert the client applies `changed` without a Get round-trip.
    void emitCardCapabilitiesChanged(uint capabilities);

    /// @brief Change the card's Capabilities and emit a PropertiesChanged that
    ///        marks the property `invalidated` (empty `changed`), forcing the
    ///        client onto its single-GetAll fallback path.
    void invalidateCardCapabilities(uint capabilities);

    /// @brief Change the card's Capabilities WITHOUT any signal — models a
    ///        client-vs-agent capability desync, so the Credentials1 entry gate
    ///        can refuse a client whose cached caps still advertise the bit.
    void setCardCapabilitiesSilently(uint capabilities);

    /// @brief How many Operation1.Cancel calls reached minted operations so far
    ///        — the observable seam for "an abandoned op was cancelled
    ///        agent-side" (a re-target must dismiss the orphaned read's prompt).
    [[nodiscard]] int cancelledOperationCount() const;
    void noteOperationCancelled();

    /// @brief Whether a ListCredentials has been issued on the card and no
    ///        mutation has invalidated it since (the agent's per-session listing
    ///        cache). ManagePin is refused UnknownCredential while false.
    [[nodiscard]] bool hasCurrentListing() const;
    /// @brief A ListCredentials was issued: mark the listing current and
    ///        snapshot the ids it returned (from the scripted records), the set
    ///        ManagePin resolves pinIds against.
    void noteListingIssued();
    /// @brief Drop the listing cache (a mutation reached the card, or a card
    ///        removal/insertion started a new session): the flag AND the id
    ///        snapshot are cleared together.
    void invalidateListing();
    /// @brief True when @p pinId is part of the CURRENT listing snapshot.
    [[nodiscard]] bool isListedId(const QString& pinId) const;

    /// @brief Re-emit ObjectManager.InterfacesAdded for the EXISTING card path
    ///        carrying changed Capabilities, so a test can assert the client
    ///        UPDATES the already-tracked card instead of dropping the new props.
    void reAddCardWithCapabilities(uint capabilities);

    /// @brief Faithful "a reader arrives already holding a card" sequence on a
    ///        SECOND reader/card path, mirroring the real agent's PresenceModel
    ///        ordering, split into three steps so the client can process (and
    ///        register its per-path match rules for) each before the next:
    ///          step (a) emitReaderArrivesEmpty: Reader1 InterfacesAdded for a NEW
    ///                   reader path with HasCard=false / Card="/";
    ///          step (b) emitArrivedReaderCardAdded: Card1 InterfacesAdded;
    ///          step (c) emitArrivedReaderHasCard: Reader1 PropertiesChanged
    ///                   flipping HasCard=true AND Card=<cardPath> together.
    ///        A reader never arrives with HasCard already true. Returns the new
    ///        card path (valid after step (b)).
    void emitReaderArrivesEmpty();
    QString emitArrivedReaderCardAdded(uint capabilities, const QString& preReadAuth = QStringLiteral("None"));
    void emitArrivedReaderHasCard();

private:
    void exportTree();

    QDBusConnection m_connection;
    Config m_config;
    QString m_rootPath = QStringLiteral("/org/librescrs/Agent");
    QString m_readerPath = QStringLiteral("/org/librescrs/Agent/reader/0");
    QString m_cardPath = QStringLiteral("/org/librescrs/Agent/card/0");
    int m_opCounter = 0;

    /// The agent's per-session listing cache marker: set by ListCredentials,
    /// cleared by any mutation (and by card removal/insertion). ManagePin is
    /// refused UnknownCredential while false — list-before-mutate is enforced.
    bool m_hasCurrentListing = false;
    /// The ids the CURRENT listing returned (snapshotted from the scripted
    /// records at list time); ManagePin refuses any pinId outside this set.
    QStringList m_currentListingIds;
    /// Operation1.Cancel calls received by minted ops (see cancelledOperationCount).
    int m_cancelledOps = 0;

    QObject* m_rootObject = nullptr;
    QObject* m_readerObject = nullptr;
    QObject* m_cardObject = nullptr;
    QObject* m_wedgedObject = nullptr;
    WedgedPropertiesAdaptor* m_wedgedProps = nullptr;
    QString m_wedgedPath = QStringLiteral("/org/librescrs/Agent/wedged/0");
    ObjectManagerAdaptor* m_objectManager = nullptr;
    ReaderAdaptor* m_readerAdaptor = nullptr;
    CardAdaptor* m_cardAdaptor = nullptr;
    Pkcs11Adaptor* m_pkcs11Adaptor = nullptr;
    QList<FakeOperation*> m_operations;

    // Second reader/card, materialised on demand by emitReaderArrivesThenCard.
    QString m_reader2Path = QStringLiteral("/org/librescrs/Agent/reader/1");
    QString m_card2Path = QStringLiteral("/org/librescrs/Agent/card/1");
    QObject* m_reader2Object = nullptr;
    QObject* m_card2Object = nullptr;
    ReaderAdaptor* m_reader2Adaptor = nullptr;
    CardAdaptor* m_card2Adaptor = nullptr;

    QString m_lastSignCertId;
    QVariantMap m_lastSignOptions;
    QByteArray m_lastSignInputBytes;

    QString m_lastCertDerReader;
    QString m_lastCertDerCertId;
};

// --- reference-out-parameter introspection ---------------------------------
//
// QtDBus splits an adaptor slot's parameters by spelling: moc normalizes a
// `const T&` input down to plain `T`, so the ones still carrying a trailing `&`
// are exactly the OUTPUT parameters. For those, and only those, QtDBus resolves
// the type by NAME — `QMetaType::fromName()` on the string moc recorded — which
// is a registry the metatype registration does not populate on its own. A name
// nothing is registered under makes QtDBus match no slot at all: the call comes
// back an error, and a caller's recovery path quietly gets nothing.
//
// Nothing about that is visible to the compiler, so these two accessors let a
// test read it. Half the fake's adaptors are file-local to the implementation
// and unreachable from a test any other way.

/// @brief Every `QDBusAbstractAdaptor` this fake defines, across both files.
///        A NEW ADAPTOR MUST BE ADDED HERE — the out-parameter pin walks exactly
///        this list, and a slot the walk never sees is a slot nothing checks.
[[nodiscard]] QList<const QMetaObject*> adaptorMetaObjects();

/// @brief The type name moc recorded for every reference OUTPUT parameter of
///        every slot on those adaptors, trailing `&` included — the exact
///        strings QtDBus will look up.
[[nodiscard]] QList<QByteArray> adaptorReferenceOutParameterTypes();

} // namespace LibreKDETest

// The struct-based mirrors, the containers built on them, and the
// managed-objects map are types no client header declares, so each takes the
// macro. FakeInterfaceProps / FakePhotoMap / FakeCredentialRecords deliberately
// do not — see the note beside their declarations.
Q_DECLARE_METATYPE(LibreKDETest::FakeManagedObjects)
Q_DECLARE_METATYPE(LibreKDETest::FakeIdentityField)
Q_DECLARE_METATYPE(LibreKDETest::FakeIdentityFieldGroup)
Q_DECLARE_METATYPE(LibreKDETest::FakeIdentityFields)
Q_DECLARE_METATYPE(LibreKDETest::FakeCertField)
Q_DECLARE_METATYPE(LibreKDETest::FakeCertFieldGroup)
Q_DECLARE_METATYPE(LibreKDETest::FakeCertFieldGroups)
Q_DECLARE_METATYPE(LibreKDETest::FakeCertInfo)
Q_DECLARE_METATYPE(LibreKDETest::FakeCertInfoList)
