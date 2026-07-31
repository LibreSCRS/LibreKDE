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

### Known issues

- Signing from LibreKDE now always asks for the baseline signature level
  (B-B), and no longer lets the agent choose. Before this change LibreKDE sent
  no level at all, and the agent applied the level it is configured with.

  Two configurations are affected, and in both the signature is produced
  successfully with no error and no warning — only at a lower conformance
  level than the agent is set up to produce. If the agent's `DefaultLevel` is
  `b-t`, `b-lt` or `b-lta`, a signature made from LibreKDE is B-B instead. If
  `DefaultLevel` is `b-b` and `TsaUrls` is configured, the agent would
  normally upgrade the signature to B-T so it carries a trusted timestamp;
  a signature made from LibreKDE is not timestamped.

  This affects signatures requested through LibreKDE only — the plasmoid's
  "Sign file", the Purpose "Sign" share plugin — and not signatures made by
  other clients of the same agent. Fixing it needs a way to say "let the agent
  decide" in the request, which is a change to the agent's client library
  rather than to LibreKDE; until then, set the level you need in the
  application you sign from, or verify the level of signatures produced this
  way if your deployment requires B-T or higher.
