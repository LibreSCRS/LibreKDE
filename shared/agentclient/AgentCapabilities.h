// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <cstdint>

/// @file
/// @brief Plain-constant mirror of the agent's `Card1.Capabilities` bitfield
///        plus the UI-state grouping helper.
///
/// The bit values are a deliberate, hand-checked copy of LibreMiddleware's
/// `LibreSCRS::Plugin::CardCapabilities` (`Plugin/PluginTypes.h`) — the agent
/// builds `Card1.Capabilities` straight from that enum, so the wire values
/// match. Mirroring them here as a pure `uint32_t` keeps `librekde-agentclient`
/// LibreMiddleware-free: a thin D-Bus client links no card core.

namespace LibreKDE {

/// @brief Pre-read unlock the card demands, mirroring Card1.PreReadAuthMethod.
///
/// Lives here (a pure, Qt-free header) rather than in AgentCard.h so the pure
/// `resolveCardState` resolver below can take it by value without pulling QtDBus.
enum class PreReadAuth {
    None,
    Mrz,
    Can,
};

/// @brief Capability bits as carried on `org.librescrs.Agent.Card1.Capabilities`.
///
/// @note Values mirror LM `CardCapabilities` exactly — do not renumber.
namespace Cap {
inline constexpr std::uint32_t None = 0;
inline constexpr std::uint32_t Pki = 1U << 0;           ///< readCertificates + verifyPIN + sign + discoverKeys.
inline constexpr std::uint32_t IdentityData = 1U << 1;  ///< readCard returns identity/document fields.
inline constexpr std::uint32_t EmrtdCrypto = 1U << 2;   ///< eMRTD-family crypto (BAC/PACE/PA/AA/CA).
inline constexpr std::uint32_t PinManagement = 1U << 3; ///< verifyPIN/changePIN/unblockPIN.
} // namespace Cap

/// @brief Coarse UI states a surface renders from a card's capability set.
///
/// The 2×2 IdentityData×PKI grouping: a card that reads
/// identity data AND signs is `Hybrid`; identity-only and PKI-only collapse to
/// their single surface; ancillary-only / empty capability sets have no
/// plasmoid-visible surface and map to `None`.
enum class UiState : std::uint32_t {
    None,            ///< No useful capability (sentinel returned only by uiStateFor()).
    NoCard,          ///< No card present (caller-supplied, never produced by uiStateFor).
    PreAuthRequired, ///< Card present but a pre-read unlock is required first.
    IdentityOnly,    ///< IdentityData without PKI.
    PkiOnly,         ///< PKI without IdentityData.
    Hybrid,          ///< Both IdentityData and PKI.
    Error,           ///< Resolution failed / capabilities unusable (ancillary-only).
    UnknownCard,     ///< Card present but the agent matched no plugin (empty capability set).
};

/// @brief True when @p flag is present in @p caps.
[[nodiscard]] constexpr bool has(std::uint32_t caps, std::uint32_t flag) noexcept
{
    return (caps & flag) != 0U;
}

/// @brief Map a capability bitfield to its coarse UI grouping.
///
/// Pure 2×2 over {IdentityData, PKI}; everything else (EmrtdCrypto,
/// PinManagement) is ancillary and does not, on its own, create a surface.
[[nodiscard]] constexpr UiState uiStateFor(std::uint32_t caps) noexcept
{
    const bool identity = has(caps, Cap::IdentityData);
    const bool pki = has(caps, Cap::Pki);
    if (identity && pki) {
        return UiState::Hybrid;
    }
    if (identity) {
        return UiState::IdentityOnly;
    }
    if (pki) {
        return UiState::PkiOnly;
    }
    return UiState::None;
}

/// @brief Resolve a card's full UI state, owning the latch logic every surface
///        would otherwise re-implement.
///
/// The single pure source of truth for "what does the user see for this card":
///   - not present                       -> NoCard
///   - present, a pre-read unlock is required AND identity not yet read
///                                        -> PreAuthRequired (the surface must
///                                           prompt for the CAN/MRZ first)
///   - otherwise the coarse uiStateFor grouping, with a None split by cause:
///     an EMPTY capability set (no plugin matched) becomes UnknownCard, while
///     an ancillary-only set (no user surface) becomes Error — both distinct
///     from "no card at all".
///
/// @param caps          Card1.Capabilities bitfield.
/// @param preAuth       Card1.PreReadAuthMethod.
/// @param present       Whether a card is present in the reader.
/// @param identityRead  Whether identity has already been read (clears the
///                      pre-auth latch once the unlock succeeded).
[[nodiscard]] constexpr UiState resolveCardState(std::uint32_t caps, PreReadAuth preAuth, bool present,
                                                 bool identityRead) noexcept
{
    if (!present) {
        return UiState::NoCard;
    }
    if (preAuth != PreReadAuth::None && !identityRead) {
        return UiState::PreAuthRequired;
    }
    const UiState grouped = uiStateFor(caps);
    if (grouped == UiState::None) {
        // A present card the agent matched no plugin for (empty capability set) is
        // a calm "unrecognized card", distinct from an ancillary-only card (e.g.
        // eMRTD-crypto / PIN-management with no IdentityData or PKI) which has no
        // user surface and is a genuine error to the user.
        return caps == 0U ? UiState::UnknownCard : UiState::Error;
    }
    return grouped;
}

} // namespace LibreKDE
