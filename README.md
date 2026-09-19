# LibreKDE

KDE Plasma 6 clients for [LibreSCRS](https://librescrs.github.io/).

LibreKDE is a set of thin Plasma clients of the per-user LibreSCRS
agent, spoken to over D-Bus (`org.librescrs.Agent`). It links no
LibreMiddleware: card I/O and secret entry live in the agent, which
loads LibreMiddleware itself. LibreKDE is in development.

## Components

| Component | Status |
|---|---|
| Plasmoid (system-tray widget) | in development |
| Credentials window (card PIN / PUK management) | implemented |
| Purpose plugin (sign from any Qt app) | implemented |
| KIO worker (`card://`) | implemented |

The credentials window (`librescrs-credentials-kde`) is a small Kirigami
app that manages a card's PINs and PUK (change / unblock / activate) by
talking to the agent — it holds no secrets and links no LibreMiddleware.
It is launched from the plasmoid, not from the application menu: its
desktop entry is `NoDisplay=true`, so it has no standalone launcher and
no separate AppStream listing.

## Build

LibreKDE builds against the LibreSCRS agent's Qt client library
(`LibreAgent::ClientQt`), which lives in its own repository. Install that
first, then point LibreKDE's configure step at the prefix you installed it
into:

```bash
# 1. The agent's Qt client library, cloned and built BESIDE this checkout so
#    nothing of it lands inside it. Core off keeps the card middleware out of
#    the build entirely — LibreKDE needs the client and nothing else.
git clone https://github.com/LibreSCRS/LibreAgent.git ../LibreAgent
cmake -B ../build-agent -S ../LibreAgent -DCMAKE_BUILD_TYPE=Release \
      -DLIBREAGENT_BUILD_CORE=OFF \
      -DLIBREAGENT_BUILD_WIRE=ON \
      -DLIBREAGENT_BUILD_CLIENT_QT=ON
cmake --build ../build-agent -j4
cmake --install ../build-agent --prefix "$PWD/../agent-prefix"

# 2. LibreKDE itself.
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$PWD/../agent-prefix"
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Leave `CMAKE_PREFIX_PATH` out if the client library is already somewhere CMake
searches — a distribution package, or `/usr/local`. Without it and without an
installed client library, step 2 stops at
`find_package(LibreAgent 5.0 ... COMPONENTS ClientQt)`; that is the missing
dependency, not a broken checkout.

To work on this repository and the agent's at the same time,
`-DLIBREKDE_FETCH_AGENT=ON` builds the client library from source as part of
this build instead of finding an installed one. It clones the agent at configure
time, so it needs network access, and it needs CMake 3.28 or newer where
everything else here needs 3.24.

The suite needs neither a display nor a session bus: the tests pin
`QT_QPA_PLATFORM=offscreen` themselves, and every test that speaks D-Bus is
registered to run under `dbus-run-session`, so it brings its own private bus.
It does need a real UTF-8 locale — one catalogue test resolves a Serbian
string through the C library's gettext, which suppresses translation under the
C locale. An `LC_ALL=C` in your environment fails that test even though the
test pins `LANG`, because `LC_ALL` outranks it.

## Build dependencies

Always needed, whichever components you build:

- CMake 3.24+, a C++23 compiler, and Ninja or Make
- `git` — the version is derived from the repository's tags at configure time
- `extra-cmake-modules` 6.0+
- LibreAgent 5.0+, built and installed with its `ClientQt` component
- Qt 6.6+ development packages: Core / Gui / Qml / DBus / Test
- KF6 6.0+ development packages: I18n / CoreAddons / Notifications / Config
- `gettext` (`msgfmt`) — the catalogues are compiled during the build

Needed per component, keyed to the CMake option that gates each one:

- `LIBREKDE_BUILD_PLASMOID` — Plasma 6.0+ (`libplasma`), KF6 KIO and Service,
  and Qt Widgets
- `LIBREKDE_BUILD_PURPOSE` — KF6 Purpose, and Qt Widgets
- `LIBREKDE_BUILD_KIO` — KF6 KIO
- `LIBREKDE_BUILD_CREDENTIALS` — KF6 Kirigami and DBusAddons, and Qt
  QuickControls2 and Widgets
- `BUILD_TESTING` (on by default) — GoogleTest, and `dbus` for the tests that
  bring up their own bus

Qt Widgets and KF6 KIO each answer to more than one component, so switching one
component off does not necessarily drop them.

## Translations

Catalogues live under `po/<language>/<domain>.po`, in two gettext domains —
`librekde` for the C++ and credentials-window strings, and
`plasma_applet_org.librescrs.smartcard` for the plasmoid's QML. Serbian is
maintained in both scripts (`sr`, `sr@latin`), Cyrillic first.

`tools/check-catalogs.py` reconciles them with the sources: it regenerates the
`.pot` files from the code as it is now and reports both a string with no
catalogue entry and a catalogue entry with no call site left. It needs
`xgettext` and `msgfmt`, runs with no arguments, and exits non-zero on either
finding; `--verbose` also prints the entry count per catalogue.

## Runtime dependencies

- A running LibreSCRS agent (`org.librescrs.Agent`)
- The agent's Qt client library (`liblibrescrs-agentclient-qt.so.5`)
- Qt 6.6+ (Core / Gui / Qml / DBus; Widgets + QuickControls2 for the
  credentials window)
- KF6 6.0+ (I18n / CoreAddons / Notifications / Config; Kirigami +
  DBusAddons for the credentials window)
- Plasma 6.0+ (per component, e.g. plasma-workspace for the plasmoid)

## Repository

LibreKDE is one of the LibreSCRS repositories; see
<https://librescrs.github.io/> for the project overview.

## Licence

LGPL-2.1-or-later — see `LICENSE`.
