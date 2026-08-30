// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <LibreSCRS/AgentClient/IdentityRows.h>
#include <LibreSCRS/AgentClient/Types.h>

#include <QList>
#include <QString>
#include <QStringList>

/// @file
/// @brief The host half of identity-row assembly: turning the frozen label KEY
///        the agent ships into a localized display string.
///
/// Structural flattening is the agent client library's job and lives there
/// (`LibreSCRS::AgentClient::flattenIdentityFields`, which produces the
/// `IdentityRow` below). That library deliberately never translates, so the
/// key→label mapping is the host's, and this is where it lives — once, for
/// every LibreKDE surface that renders identity fields.
///
/// @par Identity-render invariants (all LibreKDE surfaces)
///
/// These hold for the plasmoid popup, the `card:/` KIO worker and anything
/// added later. They are not style: each one has a defect on the other side of
/// it, and the `IdentityRenderFence` ctest case (tools/identity-render-fence.cmake)
/// fails the build's test step when a source line breaks one.
///
/// 1. **Render from the FINAL result only.** An identity read also offers a
///    progressive per-group stream while it runs. The client library documents
///    those groups as HINTS: nothing fixes their order, nothing promises the
///    set is complete, and a plugin that resolves a field late may emit it only
///    into the final result. A view assembled from that stream is an
///    order-dependent view of an unordered source — it shows a group heading
///    twice, or a field under a heading it does not belong to, on whatever card
///    happens to emit differently. No LibreKDE surface subscribes to it.
///
/// 2. **Do not depend on row order.** The order rows arrive in is the emitting
///    plugin's business and is not part of any contract this repo can enforce.
///    A renderer that groups rows must group them by group KEY (each group
///    emitted once, holding all of its rows) and not by adjacency; a renderer
///    that picks rows must state the rule it picks by. `renderIdentityTxt`
///    keeps groups in first-appearance order for a stable file, which is a
///    choice of output shape, not a claim about the input.
///
/// 3. **Render each row once per view.** A curated summary is a SUBSET of the
///    full model, so a view that shows both must SUBTRACT one from the other
///    (the plasmoid publishes `identityDetails` = fields − summary for exactly
///    this) — otherwise every summarised field is drawn a second time as soon
///    as the details are revealed. Subtract by the row's (groupKey, fieldKey)
///    identity, never by its rendered text: the same key can legitimately
///    appear under two groups, and text matching would drop both copies.

namespace LibreKDE {

/// @brief Assemble an identity read's field groups into the rows a LibreKDE
///        surface draws: one row per security check, everything else flattened
///        as it arrived.
///
/// The wire SHAPE of a security check — which keys make up one check, which
/// keys are the group's aggregate roll-ups, what an unrecognised suffix means,
/// and which groups carry verdicts at all — is deliberately not read here.
/// `LibreSCRS::AgentClient::separateSecurityChecks` owns it, once, for every
/// client that builds that library. This repository used to carry a second
/// reader of the same shape: two readers agree only until one of them is
/// taught something, and each was pinned by its own tests, so neither ever
/// noticed that it had drifted.
///
/// Every group is piped through the library, not only the ones this file would
/// have guessed are verdict groups — a group the library does not claim comes
/// back untouched, so the scope rule stays in the one place that owns it.
///
/// What remains here is the half the library refuses to do, because it never
/// translates:
///
///  - `check_N_reason` is a KEY, and becomes the instruction a reader acts on
///    through `localizedCheckReason` below;
///  - the row's value is spelled `"STATUS (explanation)"` — the joined shape's
///    own spelling — so `localizedFieldValue` translates either shape with no
///    second case;
///  - a reason SUPERSEDES a detail rather than joining it: two parentheticals
///    on one row is noise, and a producer that ships a reason has already
///    replaced its English sentence with the key;
///  - `category` and `error`, which the library separates faithfully, are not
///    rendered. This build has no vocabulary for either, and a raw producer
///    token on screen is a machine key put in front of a person.
///
/// Order: a group's checks come first, in the library's ascending numeric
/// ordinal, then every field it did not consume, in arrival order; groups keep
/// their own order. Stated so the assembly is describable — invariant 2 above
/// still stands, and no view may depend on it.
[[nodiscard]] QList<LibreSCRS::AgentClient::IdentityRow>
identityRows(const QList<LibreSCRS::AgentClient::FieldGroup>& groups);

/// @brief Resolve a security check's `check_N_reason` KEY into the sentence a
///        reader is meant to act on.
///
/// A verdict alone strands the reader: "Not performed" over a travel document
/// says nothing about whether the document is suspect or the machine simply has
/// no trust anchors yet, and those two want opposite responses. The plugin
/// therefore ships a frozen reason key instead of an English sentence, and this
/// is where that key becomes copy that names the REMEDY — "import a master
/// list", "check the directory's permissions" — rather than restating the
/// condition.
///
/// Resolution order is `localizedFieldLabel`'s, for the same reason: the
/// translated instruction for a KNOWN key; else the producer-authored
/// @p detail; else the raw key. Never empty and never the word "unknown" — a
/// newer agent may name a reason this build has never heard of, and blanking
/// the explanation would hide a verdict that is being reported correctly.
[[nodiscard]] QString localizedCheckReason(const QString& reasonKey, const QString& detail = {});

/// @brief Every reason key the table maps, so a test can pin the set.
[[nodiscard]] QStringList mappedCheckReasonKeys();

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

/// @brief Should this row be kept out of the display entirely?
///
/// The eID plugin reports three chip-signature verification outcomes as
/// ordinary identity fields, each labelled with its own raw key. They are an
/// internal diagnostic, so both surfaces drop them. An exact-key set, not a
/// suffix match — a future `*_verification` field is a new product decision.
[[nodiscard]] bool isHiddenIdentityRow(const LibreSCRS::AgentClient::IdentityRow& row);

/// @brief Resolve an identity field's display VALUE, localized.
///
/// Card data stays byte-faithful in the middleware (the chip signs those
/// bytes), so an `address_date` the card left as a placeholder arrives
/// verbatim; here it becomes the localized "Unknown". Every other field passes
/// through untouched — a blanket date check would blank values that other
/// cards legitimately carry.
[[nodiscard]] QString localizedFieldValue(const LibreSCRS::AgentClient::IdentityRow& row);

/// @brief Resolve a GROUP's display heading, localized; empty when this build
///        has no name for the group.
///
/// A heading is what ties a verdict to the data it describes. A card can report
/// the travel document's passive authentication (checked against a known
/// issuer) and an annex's integrity-only result (no trust anchor exists for an
/// annex yet) in the same read; rendered as adjacent rows of one flat list they
/// read as a single guarantee, and the annex gets credited with a check nobody
/// ran.
///
/// Empty means "render no heading", NOT "render an empty one": an unknown group
/// still shows its rows. Annex groups are matched on the `annex.` PREFIX, since
/// their keys carry an id in the middle that comes from the reader.
[[nodiscard]] QString localizedGroupLabel(const QString& groupKey);

/// @brief Every group key the heading table maps, so a test can pin the set.
[[nodiscard]] QStringList mappedGroupKeys();

/// @brief Reading order for a group's fields, or empty to keep delivery order.
///
/// Delivery order is NOT a display order. Identity crosses the wire as
/// map-of-maps, so fields arrive sorted by KEY — which for a group whose
/// substance is an address puts the street last and the apartment third.
///
/// A key outside the returned list keeps its relative position AFTER every
/// listed one, the same rule the group staging applies.
[[nodiscard]] QStringList fieldOrderForGroup(const QString& groupKey);

/// @brief Every `labelKey` the table maps, so a test can pin the set's size.
[[nodiscard]] QStringList mappedLabelKeys();

} // namespace LibreKDE
