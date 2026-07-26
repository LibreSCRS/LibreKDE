// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Pure capability → UI-state classification. No D-Bus, no LibreMiddleware:
// the classifier consumes the agent client's `LibreKDE::Cap` mirror.

#include "AgentCapabilities.h"
#include "CardStateModel.h"

#include <gtest/gtest.h>

namespace Cap = LibreKDE::Cap;
using LibreKDE::UiState;
using LibreKDE::Plasmoid::CardStateModel;

TEST(CardStateModelClassify, NoCapabilitiesYieldsUnknownCard)
{
    // A present card the agent matched no plugin for (empty capability set) is a
    // calm "unrecognised card", NOT an error.
    EXPECT_EQ(CardStateModel::classify(Cap::None), CardStateModel::State::UnknownCard);
}

TEST(CardStateModelClassify, IdentityOnly)
{
    EXPECT_EQ(CardStateModel::classify(Cap::IdentityData), CardStateModel::State::IdentityOnly);
}

TEST(CardStateModelClassify, PkiOnly)
{
    EXPECT_EQ(CardStateModel::classify(Cap::Pki), CardStateModel::State::PkiOnly);
}

TEST(CardStateModelClassify, HybridIdentityPlusPki)
{
    EXPECT_EQ(CardStateModel::classify(Cap::Pki | Cap::IdentityData), CardStateModel::State::Hybrid);
}

TEST(CardStateModelClassify, PinManagementWithoutIdentityOrPkiYieldsError)
{
    // Ancillary-only capability sets have no plasmoid-visible surface today.
    EXPECT_EQ(CardStateModel::classify(Cap::PinManagement), CardStateModel::State::Error);
}

TEST(CardStateModelClassify, EmrtdOnlyYieldsError)
{
    EXPECT_EQ(CardStateModel::classify(Cap::EmrtdCrypto), CardStateModel::State::Error);
}

TEST(CardStateModelClassify, IdentityPlusEmrtdRendersIdentityOnly)
{
    EXPECT_EQ(CardStateModel::classify(Cap::IdentityData | Cap::EmrtdCrypto), CardStateModel::State::IdentityOnly);
}

TEST(CardStateModelClassify, AllCapabilitiesRendersHybrid)
{
    const auto everything = Cap::Pki | Cap::IdentityData | Cap::EmrtdCrypto | Cap::PinManagement;
    EXPECT_EQ(CardStateModel::classify(everything), CardStateModel::State::Hybrid);
}

TEST(CardStateModelFromUiState, LifecycleStatesPassThrough)
{
    EXPECT_EQ(CardStateModel::fromUiState(UiState::NoCard), CardStateModel::State::NoCard);
    EXPECT_EQ(CardStateModel::fromUiState(UiState::PreAuthRequired), CardStateModel::State::PreAuthRequired);
    EXPECT_EQ(CardStateModel::fromUiState(UiState::Error), CardStateModel::State::Error);
    EXPECT_EQ(CardStateModel::fromUiState(UiState::None), CardStateModel::State::Error);
}

TEST(CardStateModelFromUiState, UnknownCardPassesThrough)
{
    EXPECT_EQ(CardStateModel::fromUiState(UiState::UnknownCard), CardStateModel::State::UnknownCard);
}
