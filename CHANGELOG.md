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
  the generated one is whatever the forge makes of a tag name, its bytes are
  not ours to assert, and the recipe asked for it under `sha256sums=('SKIP')`,
  so that line said nothing about what was actually built. The published
  tarball is a function of the commit — every member carries the commit's own
  timestamp, owner `0/0` and a mode no umask can widen — so a packager who
  rebuilds it gets the same bytes back, up to the gzip implementation. It is
  named so that one file can serve as the `.orig` for `dpkg-source`; the `deb`
  and `rpm` builds still build from the checkout and do not consume it yet.

- **Five distribution packages for the KDE integration.** The plasmoid, the
  `card:/` KIO worker, the Purpose "Sign" plugin, the credential window, and an
  architecture-independent package carrying the message catalogues and the
  AppStream metadata the other four share. These are compiled objects living in
  system plugin directories, so no bundle format can deliver them.

  The previous recipes required the middleware, which this project does not link
  at all — it talks to the agent through the Qt client library — and packaged two
  of the five artefacts, leaving the KIO worker, the Purpose plugin and every
  message catalogue unpackaged.

- **One package name installs all of it.** `librekde` is a metapackage that
  depends on the five packages above, each at exactly its own version, so
  `apt install librekde` or `dnf install librekde` brings in the plasmoid, the
  KIO worker, the Purpose plugin, the credential window and the data they
  share. Each part can still be installed on its own.


### Removed

- Two build options that named components this repository does not contain:
  `LIBREKDE_BUILD_KRUNNER` and `LIBREKDE_BUILD_KWALLET`. There is no
  `components/krunner` and no KWallet backend here, and each option's only
  other mention was the status line that printed its value.

- The `LIBREKDE_BUILD_PLASMOID`, `LIBREKDE_BUILD_PURPOSE`, `LIBREKDE_BUILD_KIO`
  and `LIBREKDE_BUILD_CREDENTIALS` switches. Every component is always built:
  each package ships all four, and nothing built or tested the configurations
  that left one out.

- The `coverage` CMake preset and `gcovr.cfg`. No job or script ran them, and
  a coverage figure is a property of whatever configuration measured it.

- Metadata nothing needs: the widget's `ServiceTypes` key (a Plasma 5 key
  Plasma 6 does not read), its `X-Plasma-Provides` key, which advertised an
  alternative no applet offers, and the KIO worker's `X-DocPath`, which
  pointed at a handbook page that was never written.

### Changed

- Building the agent client library from source along with this build is now
  `-DLIBRESCRS_AGENT_FROM_SOURCE=ON`, the switch every LibreSCRS repository
  uses; `-DLIBREKDE_FETCH_AGENT` is gone.

- Built against the agent client whose certificates carry each key's ID, type,
  size and key-agreement capability (new trailing members of the D-Bus
  certificate result); no visible change.

- Built against the agent client whose certificates carry the card's
  per-operation consent rule (a new trailing member of the D-Bus certificate
  result); no visible change.

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

- The Plasma widget, the credential manager and the card reader's
  `info.txt` in the file manager name a card reader the way the card agent
  does — the same short model, and "contact" or "contactless" on a reader
  with both, that the PIN prompt shows — instead of the reader's long system
  name or a name worked out from it by a rule of its own. Two identical
  readers are told apart by the agent, and a label follows it when a reader
  beside it is plugged in or removed.

### Fixed

- Signing from the Plasma widget no longer replaces a file that is already
  there. A document `contract.pdf` next to an existing `contract-signed.pdf` —
  someone else's signed copy, another tool's output — used to have that file
  silently overwritten. The signature now goes to the first free name instead
  (`contract-signed (2).pdf`), from the widget and from the "Sign" share
  action alike, which no longer asks whether to overwrite. Signatures are also
  named the way LibreCelik names them, so the same document signed from either
  gets the same file: a detached XML signature is `<name>.xsig` (was
  `<name>.xml`), a JSON one `<name>.jose` (was `<name>.json`), and an ASiC-E
  container keeps its own name instead of gaining `-signed`.
  A signature interrupted while it is being saved no longer leaves a cut-off
  file under the signed name, and an empty answer from the agent is reported
  as an empty signature instead of being saved as an empty "signed" file.

- "Start service" in the Plasma widget starts the card agent through the
  session bus, the way every other client of the agent does, and says when it
  could not be started. It used to run `systemctl` in the background and never
  learned whether that worked.
  The button, and the same action in the credential window, is disabled while
  the service starts, so a second click no longer sends a second request.

- Clicking "Start service" or "Refresh" in the Plasma widget no longer takes
  the Plasma shell down with it. The click waited for the service, and when
  the service came up or a card arrived meanwhile, the widget replaced the view
  holding the button before the click had been handled.

- Failures from the card agent read the same in the Plasma widget, the
  `card:/` folder, the credential window and the "Sign" action as they do in
  LibreCelik, in your own language where the agent names the failure: a
  cancelled operation and an out-of-date PIN entry helper no longer show the
  agent's English sentence, a signing-engine error keeps the agent's own
  explanation below the translated headline, and a request the agent refused
  says why. Dismissing the PIN or CAN prompt in the widget returns to the
  unlock button instead of showing an error page.

- When the card agent is not there, every Plasma surface says the same thing
  and offers the same next step: the widget and the credential window tell a
  service that is installed but not running (with a "Start service" button)
  apart from one that is not installed (with a link to the installation guide
  in your language, rather than the website's front page), and the `card:/`
  folder says which of the two it is instead of showing an empty folder that
  reads as "no card inserted".

- A card read the agent stops reporting on no longer leaves the Plasma widget
  busy for good, with its "Read card" button disabled until plasmashell is
  restarted; the read ends with an error after the same quiet period LibreCelik
  and the `card:/` folder allow. The credential window and the "Sign" action
  are bounded the same way. Time spent at the PIN or CAN prompt never counts.

- The Plasma widget no longer keeps every finished card read — with the
  holder's personal data in it — in plasmashell's memory until the card is
  removed.

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
