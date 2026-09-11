<!--
SPDX-License-Identifier: LGPL-2.1-or-later
SPDX-FileCopyrightText: 2026 hirashix0
-->

# Changelog

Notable changes to LibreKDE, newest first. There is no tagged release yet, so
every entry below describes a change to what you get by building from source.

## [Unreleased] — 5.0.0

### Added

- **Every release carries a source tarball this project built.** The Arch
  recipe fetches that asset instead of the archive GitHub generates for a tag:
  the generated one omits every submodule tree, and its bytes are not ours to
  assert, so the recipe's `sha256sums` line said nothing about what was
  actually built. The published tarball is a function of the commit — every
  member carries the commit's own timestamp, owner `0/0` and a mode no umask
  can widen — so a packager who rebuilds it gets the same bytes back, up to the
  gzip implementation. It is named so that one file can serve as the `.orig`
  for `dpkg-source`; the `deb` and `rpm` builds still build from the checkout
  and do not consume it yet.

- **Five distribution packages for the KDE integration.** The plasmoid, the
  `card:/` KIO worker, the Purpose "Sign" plugin, the credential window, and an
  architecture-independent package carrying the message catalogues and the
  AppStream metadata the other four share. These are compiled objects living in
  system plugin directories, so no bundle format can deliver them.

  The previous recipes required the middleware, which this project does not link
  at all — it talks to the agent through the Qt client library — and packaged two
  of the five artefacts, leaving the KIO worker, the Purpose plugin and every
  message catalogue unpackaged.


### Removed

- Two build options that named components this repository does not contain:
  `LIBREKDE_BUILD_KRUNNER` and `LIBREKDE_BUILD_KWALLET`. There is no
  `components/krunner` and no KWallet backend here, and each option's only
  other mention was the status line that printed its value.

### Changed

- The Plasma clients no longer carry their own D-Bus client for the card
  agent. Card access, the transport and the wire value types now come from the
  agent project's Qt client library, and LibreKDE keeps only the part that
  library deliberately does not do: turn its error taxonomy, its credential
  vocabulary and its identity field keys into localized, user-facing text.

  If you build or package LibreKDE, this is the change that affects you.
  Configuring with a bare `cmake -B build` no longer works: the build now needs
  LibreAgent 5.0 or newer, installed with its `ClientQt` component, either
  where CMake already looks or named through `CMAKE_PREFIX_PATH`. The build
  section of the README has the full recipe. The plugins and executables that
  come out link `liblibrescrs-agentclient-qt.so.5`, a versioned shared object,
  so a package needs a runtime dependency on it and LibreKDE has to be rebuilt
  when that library's major version changes.

### Fixed

- The application now reports the version it actually is. The About window and
  `--version` said 0.1.0, and the Plasma widget's information said 0.1.0, while the
  package that installed them was labelled 5.0.0.

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
