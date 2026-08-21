// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#include "CredentialText.h"
#include <KLocalizedString>
namespace LibreKDE::CredentialText {
// File-local short spellings for the agent client library's credential
// vocabulary. Every switch below is exhaustive with NO `default:` arm, and that
// is deliberate and safe here — unlike `ErrorCode`, these enums are closed on
// the decode path: the library maps every wire token it does not recognise onto
// `Unknown` / `Unspecified` before a caller sees it, so no out-of-range value
// can arrive. Omitting `default:` therefore keeps a `-Wswitch` diagnostic
// pointing at this file when an enumerator is appended. The trailing `return {}`
// after each switch exists for the compiler (a function must return on every
// path); the coverage tests assert no enumerated value reaches it.
using LibreSCRS::AgentClient::CredentialKind;
using LibreSCRS::AgentClient::CredentialOutcome;
using LibreSCRS::AgentClient::CredentialState;

QString kindName(CredentialKind k)
{
    switch (k) {
    case CredentialKind::User:
        return ki18ndc("librekde", "credential kind", "User PIN").toString();
    case CredentialKind::Sign:
        return ki18ndc("librekde", "credential kind", "Signing PIN").toString();
    case CredentialKind::Puk:
        return ki18ndc("librekde", "credential kind", "PUK").toString();
    case CredentialKind::Can:
        return ki18ndc("librekde", "credential kind", "CAN").toString();
    case CredentialKind::Unknown:
        return ki18ndc("librekde", "credential kind", "Credential").toString();
    }
    return {};
}
QString stateName(CredentialState s)
{
    switch (s) {
    case CredentialState::Transport:
        return ki18nd("librekde", "Transport — activation needed").toString();
    case CredentialState::Operational:
        return ki18nd("librekde", "Operational").toString();
    case CredentialState::NeedsChange:
        return ki18nd("librekde", "Change required").toString();
    case CredentialState::Blocked:
        return ki18nd("librekde", "Blocked").toString();
    case CredentialState::Unknown:
        return ki18nd("librekde", "Unknown").toString();
    }
    return {};
}
bool isNeutral(CredentialOutcome o)
{
    return o == CredentialOutcome::Ok || o == CredentialOutcome::UserCancelled;
}
QString outcomeMessage(CredentialOutcome o, CredentialKind presented, std::optional<int> retriesLeft)
{
    const QString who = kindName(presented);
    switch (o) {
    case CredentialOutcome::Ok:
        return ki18nd("librekde", "Done.").toString();
    case CredentialOutcome::UserCancelled:
        return {}; // neutral — no error banner
    case CredentialOutcome::InvalidPin:
        // Surface the attribution counter when the result delivered it
        // ("The User PIN was not correct — 2 attempts left."). Count-bearing, so
        // it rides the plural machinery (Serbian needs three forms).
        if (retriesLeft.has_value()) {
            return ki18ndcp("librekde", "%1 is the credential name, %2 the remaining attempts",
                            "The %1 was not correct — %2 attempt left.", "The %1 was not correct — %2 attempts left.")
                .subs(who)
                .subs(*retriesLeft)
                .toString();
        }
        return ki18nd("librekde", "The %1 was not correct.").subs(who).toString();
    case CredentialOutcome::Blocked:
        return ki18nd("librekde", "The %1 is now blocked.").subs(who).toString();
    case CredentialOutcome::MissingFields:
        return ki18nd("librekde", "A required value was not entered.").toString();
    case CredentialOutcome::KeyActivationFailed:
        return ki18nd("librekde", "The PIN was set, but activating the signing key failed.").toString();
    case CredentialOutcome::Unsupported:
        return ki18nd("librekde", "This action isn't available on this card.").toString();
    case CredentialOutcome::CardRemoved:
        return ki18nd("librekde", "The card was removed before the operation finished.").toString();
    case CredentialOutcome::EntryExpired:
        return ki18nd("librekde", "The entry window closed before a code was entered. Try again.").toString();
    case CredentialOutcome::PluginError:
        return ki18nd("librekde", "The card reported an error.").toString();
    case CredentialOutcome::Unspecified:
        return ki18nd("librekde", "The operation did not complete.").toString();
    }
    return {};
}
QString guidance(const std::optional<QString>& key, const std::optional<QString>& fallback)
{
    if (key && !key->isEmpty()) {
        const QString t = ki18nd("librekde", key->toUtf8().constData()).toString();
        if (t != *key)
            return t; // a translation exists → use it
    }
    return fallback.value_or(QString()); // else the agent's English fallback
}
} // namespace LibreKDE::CredentialText
