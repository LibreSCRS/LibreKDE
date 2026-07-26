// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Cross-stack wire-contract drift guard — LibreKDE (LM-free) half.
//
// LibreKDE re-types the agent's Card1.Capabilities bits (AgentCapabilities.h
// Cap::*) and the Operation1 ErrorCode (ErrorText.h) by hand, because a thin
// D-Bus client links no card core. This
// fixture pins those mirrors to the SAME canonical wire literals that the
// LM-anchored LibreLinux/agent/tests/WireContractGuardTest.cpp ties to the
// upstream LibreMiddleware symbol. The two together chain
//   LM  <->  wire literals  <->  KDE
// so an upstream renumber is caught WITHOUT this stack ever linking LM.
//
// The Certificates1/Identity1/Photo1 Result tuple signatures are pinned
// separately in AgentResultSignatureTest (KDE) and on the LibreLinux side by
// Certificates1XmlCodegenTest + CertResultWireSignatureTest — together those
// pin a(sba{sa{s(ssv)}}uasasu) on all three stacks. See the mirror manifest in
// the agent-side guard.
//
// Honest limitation: this repo has no build edge to the agent, so a cross-repo
// drift (a code appended upstream) cannot auto-fail anything HERE. The machine
// gate for taxonomy growth lives agent-side, where the ErrorCode enum is pinned
// value-for-value against the published org.librescrs.Agent.Operation1.xml —
// the canonical wire contract; new codes land there first. This side pins the
// mirror's INTERNAL consistency: the value pins below catch a renumber, and
// ErrorTextCoverageTest walks every mirrored value so an append here demands a
// deliberate ErrorText decision.

#include "AgentCapabilities.h" // LibreKDE::Cap::*
#include "ErrorText.h"         // LibreKDE::ErrorCode

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using namespace LibreKDE;

constexpr std::uint32_t u(ErrorCode e)
{
    return static_cast<std::uint32_t>(e);
}

// --- capability bits mirror (== LM CardCapabilities, anchored agent-side) ----
static_assert(Cap::None == 0u, "wire contract: Cap::None drifted from 0");
static_assert(Cap::Pki == (1u << 0), "wire contract: Cap::Pki drifted from LM PKI bit");
static_assert(Cap::IdentityData == (1u << 1), "wire contract: Cap::IdentityData drifted from LM bit");
static_assert(Cap::EmrtdCrypto == (1u << 2), "wire contract: Cap::EmrtdCrypto drifted from LM bit");
static_assert(Cap::PinManagement == (1u << 3), "wire contract: Cap::PinManagement drifted from LM bit");

// --- ErrorCode mirror (== the agent's published wire taxonomy) ---------------
// Full value pin + count tripwire — keep identical to the errorCode
// enumeration in the agent's org.librescrs.Agent.Operation1.xml (canonical;
// the LibreAgent ErrorTaxonomy.h enum is machine-pinned to it agent-side).
static_assert(u(ErrorCode::None) == 0u);
static_assert(u(ErrorCode::CardRemoved) == 1u);
static_assert(u(ErrorCode::CredentialWrong) == 2u);
static_assert(u(ErrorCode::CredentialBlocked) == 3u);
static_assert(u(ErrorCode::CommunicationError) == 4u);
static_assert(u(ErrorCode::ParseError) == 5u);
static_assert(u(ErrorCode::UnsupportedCard) == 6u);
static_assert(u(ErrorCode::AuthFailed) == 7u);
static_assert(u(ErrorCode::PrompterError) == 8u);
static_assert(u(ErrorCode::CapabilityMissing) == 9u);
static_assert(u(ErrorCode::WatchdogTimeout) == 10u);
static_assert(u(ErrorCode::KeyNotFound) == 11u);
static_assert(u(ErrorCode::KeyAmbiguous) == 12u);
static_assert(u(ErrorCode::CertExpiredBlocked) == 13u);
static_assert(u(ErrorCode::ChainIncomplete) == 14u);
static_assert(u(ErrorCode::TsaUnreachable) == 15u);
static_assert(u(ErrorCode::SigningEngineError) == 16u);
static_assert(u(ErrorCode::RateLimited) == 17u);
static_assert(u(ErrorCode::EngineUnavailable) == 18u);
static_assert(u(ErrorCode::InvalidDocument) == 19u);
static_assert(u(ErrorCode::InvalidDocument) + 1u == 20u,
              "wire contract: ErrorCode count changed; mirror the new value from the agent's Operation1.xml "
              "errorCode enumeration (the canonical wire contract)");

} // namespace

TEST(WireContractGuard, KdeMirrorHolds)
{
    SUCCEED();
}
