// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "AgentOperation.h" // LibreKDE::IdentityFields

#include <QList>
#include <QString>

namespace LibreKDE {

/// @brief One flattened Identity1 field, neutral of any presentation layer.
///        Both the card:/ KIO worker (adapts to `IdentityFieldView`) and the
///        plasmoid (adapts to a QVariantMap row) consume this — the single
///        skip-binary / stringify rule lives here, reused ≥2×.
struct IdentityRow
{
    QString groupKey;      ///< Group key (e.g. "personal").
    QString fieldKey;      ///< Field key (e.g. "given_name").
    QString labelKey;      ///< Frozen i18n key the agent ships (e.g. "field.surname"); resolver input.
    QString labelFallback; ///< Agent-authored English label (display fallback only).
    QString value;         ///< Stringified value; binary fields are dropped entirely.
};

/// @brief Flatten group → (field → IdentityField) into a neutral row list.
///        Skips `type == "binary"` fields (raw photos etc.); stringifies the
///        QDBusVariant value of every text/date field. Empty values are
///        RETAINED (the KIO renderer shows them); a caller that wants them
///        dropped filters locally.
[[nodiscard]] QList<IdentityRow> flattenIdentityFields(const IdentityFields& fields);

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
[[nodiscard]] QString localizedFieldLabel(const IdentityRow& row);

} // namespace LibreKDE
