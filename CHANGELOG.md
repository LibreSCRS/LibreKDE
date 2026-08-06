<!--
SPDX-License-Identifier: LGPL-2.1-or-later
SPDX-FileCopyrightText: 2026 hirashix0
-->

# Changelog

Notable changes to LibreKDE, newest first. There is no tagged release yet, so
every entry below describes a change to what you get by building from source.

## Unreleased

### Changed

- The Plasma clients no longer carry their own D-Bus client for the card
  agent. Card access, the transport and the wire value types now come from the
  agent project's Qt client library, and LibreKDE keeps only the part that
  library deliberately does not do: turn its error taxonomy, its credential
  vocabulary and its identity field keys into localized, user-facing text.

  If you build or package LibreKDE, this is the change that affects you.
  Configuring with a bare `cmake -B build` no longer works: the build now needs
  LibreAgent 4.2 or newer, installed with its `ClientQt` component, either
  where CMake already looks or named through `CMAKE_PREFIX_PATH`. The build
  section of the README has the full recipe. The plugins and executables that
  come out link `liblibrescrs-agentclient-qt.so.4`, a versioned shared object,
  so a package needs a runtime dependency on it and LibreKDE has to be rebuilt
  when that library's major version changes.

### Fixed

- The card details no longer list three internal chip-signature checks among
  your personal data. They arrived labelled with their own internal names,
  reading "unknown", and each was listed twice — they are a diagnostic about
  the card's own integrity, not something a card holder can read or act on, so
  they are no longer shown. The card's type is now named in your own language
  rather than in English, and so is the date the address last changed.
- A card that carries no address-change date now says so, in your own language,
  instead of showing the placeholder the chip stores in that spot. What the
  card returned is still passed through untouched everywhere else; only this
  one field, and only when it holds something that is not a date, is replaced
  by readable text.
- Signing from LibreKDE no longer forces the baseline signature level. The
  agent applies the level it is configured with, and upgrades a baseline
  default to a timestamped one when a timestamp authority is set, the same
  way it does for every other client. Until now every signature made from
  LibreKDE came out at B-B regardless — successfully, with no error and no
  warning, but at a lower conformance level than the deployment was set up
  to produce. A signature's level is now also shown when it is written.
- A failure that never got an answer out of the card agent is now reported in
  your own language, and says which of these it was: the service could not be
  reached at all, it did not answer in time, permission to use it was denied,
  the request was refused before any work started, the connection broke while
  the request was in flight, or its reply could not be understood. Until now
  only failures that came back carrying one of the agent's own error codes had
  translated copy of their own; everything else was shown as whatever text the
  failure happened to arrive with, which is untranslated and is sometimes a
  transport diagnostic rather than something written for a reader. A failure
  that none of these names, and that arrived with no message of its own, now
  ends in a plain sentence saying exactly that, rather than in nothing.
