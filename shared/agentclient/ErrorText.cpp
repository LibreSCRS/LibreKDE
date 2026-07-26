// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "ErrorText.h"

#include <KLocalizedString>

namespace LibreKDE::ErrorText {

QString forCode(ErrorCode code, const QString& msgFallback)
{
    // ki18nd("librekde", msgid).toString() yields the translation or the
    // English msgid on a catalog miss — never empty. Only the codes a client
    // can meaningfully phrase synchronously are localized here; everything else
    // (and None) defers to the agent-authored msgFallback.
    switch (code) {
    case ErrorCode::CardRemoved:
        return ki18nd("librekde", "The card was removed before the operation finished.").toString();
    case ErrorCode::CredentialWrong:
        return ki18nd("librekde", "The PIN or access code entered was incorrect.").toString();
    case ErrorCode::CredentialBlocked:
        return ki18nd("librekde", "The card credential is blocked. Unblock it before retrying.").toString();
    case ErrorCode::CommunicationError:
        return ki18nd("librekde", "Communication with the card reader failed.").toString();
    case ErrorCode::ParseError:
        return ki18nd("librekde", "The data read from the card could not be interpreted.").toString();
    case ErrorCode::UnsupportedCard:
        return ki18nd("librekde", "This card is not supported.").toString();
    case ErrorCode::AuthFailed:
        return ki18nd("librekde", "Authentication with the card failed.").toString();
    case ErrorCode::PrompterError:
        return ki18nd("librekde", "The secure entry prompt could not be shown.").toString();
    case ErrorCode::CapabilityMissing:
        return ki18nd("librekde", "This card does not support the requested operation.").toString();
    case ErrorCode::WatchdogTimeout:
        return ki18nd("librekde", "The operation timed out.").toString();
    case ErrorCode::KeyNotFound:
        return ki18nd("librekde", "The selected certificate could not be found on the card.").toString();
    case ErrorCode::KeyAmbiguous:
        return ki18nd("librekde", "More than one key matched the selection.").toString();
    case ErrorCode::CertExpiredBlocked:
        return ki18nd("librekde", "The signing certificate has expired.").toString();
    case ErrorCode::ChainIncomplete:
        return ki18nd("librekde", "The certificate chain could not be completed.").toString();
    case ErrorCode::TsaUnreachable:
        return ki18nd("librekde", "The timestamp authority is unreachable.").toString();
    case ErrorCode::SigningEngineError:
        // The catch-all engine code defers to the agent's own message when it
        // carried one, matching the None-defers-to-msgFallback rule below.
        // Today the agent's msgFallback for this code is usually the LM's own
        // (near-identical, English) generic, so the practical win is small — the
        // common deployment failure is separated out to its own LOCALIZED
        // EngineUnavailable code. This branch keeps the door open for
        // an agent that sends a genuinely specific message on this code without
        // the client having to hardcode-flatten it.
        return msgFallback.isEmpty() ? ki18nd("librekde", "The signing engine reported an error.").toString()
                                     : msgFallback;
    case ErrorCode::InvalidDocument:
        return ki18nd("librekde", "The document you tried to sign is invalid or could not be read. Check the file.")
            .toString();
    case ErrorCode::EngineUnavailable:
        return ki18nd("librekde",
                      "The signing service is not set up correctly — its security module could not be loaded. "
                      "Check the installation.")
            .toString();
    case ErrorCode::RateLimited:
        return ki18nd("librekde", "Too many signing requests. Try again shortly.").toString();
    case ErrorCode::None:
        break;
    }
    return msgFallback;
}

} // namespace LibreKDE::ErrorText
