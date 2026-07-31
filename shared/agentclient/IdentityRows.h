// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <LibreSCRS/AgentClient/IdentityRows.h>

#include <QString>

/// @file
/// @brief The host half of identity-row assembly: turning the frozen label KEY
///        the agent ships into a localized display string.
///
/// Structural flattening is the agent client library's job and lives there
/// (`LibreSCRS::AgentClient::flattenIdentityFields`, which produces the
/// `IdentityRow` below). That library deliberately never translates, so the
/// key→label mapping is the host's, and this is where it lives — once, for
/// every LibreKDE surface that renders identity fields.

namespace LibreKDE {

/// @brief Resolve an identity field's display label, localized.
///
/// The agent freezes a stable i18n key (`labelKey`, e.g. `"field.surname"`)
/// alongside its English `labelFallback` on every field. This maps that frozen
/// key onto a translated label via a compile-time table of `ki18ndc()` calls in
/// the `librekde` domain (extractable by Messages.sh; resolved at the caller's
/// current language). BOTH the plasmoid `rebuildIdentityModel` and the `card:/`
/// KIO worker (AgentCardDataSource) call this — the single localization rule
/// lives here, reused ≥2×.
///
/// Resolution order: the translated label for a KNOWN `labelKey`; else the
/// agent-authored `labelFallback`; else the raw `fieldKey` (never empty).
[[nodiscard]] QString localizedFieldLabel(const LibreSCRS::AgentClient::IdentityRow& row);

} // namespace LibreKDE
