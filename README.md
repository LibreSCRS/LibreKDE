# LibreKDE

KDE Plasma 6 clients for [LibreSCRS](https://librescrs.github.io/).

LibreKDE is a set of thin Plasma clients of the per-user LibreSCRS
agent, spoken to over D-Bus (`org.librescrs.Agent`). It links no
LibreMiddleware: card I/O and secret entry live in the agent, which
loads LibreMiddleware itself. LibreKDE is in development and
unreleased (0.1.x).

## Components

| Component | Status |
|---|---|
| Plasmoid (system-tray widget) | in development |
| Credentials window (card PIN / PUK management) | implemented |
| Purpose plugin (sign from any Qt app) | implemented |
| KIO worker (`card://`) | implemented |
| KRunner plugin | not started |
| KWallet backend (scute path) | not started |

The credentials window (`librescrs-credentials-kde`) is a small Kirigami
app that manages a card's PINs and PUK (change / unblock / activate) by
talking to the agent — it holds no secrets and links no LibreMiddleware.
It is launched from the plasmoid, not from the application menu: its
desktop entry is `NoDisplay=true`, so it has no standalone launcher and
no separate AppStream listing.

## Build

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

## Runtime dependencies

- A running LibreSCRS agent (`org.librescrs.Agent`)
- Qt 6.6+ (Core / Gui / Qml / DBus; Widgets + QuickControls2 for the
  credentials window)
- KF6 6.0+ (I18n / CoreAddons / Notifications / Config; Kirigami +
  DBusAddons for the credentials window)
- Plasma 6.0+ (per component, e.g. plasma-workspace for the plasmoid)

## Repository

LibreKDE is one of four sibling repositories under the LibreSCRS
umbrella; see <https://librescrs.github.io/> for the project overview.

## Licence

LGPL-2.1-or-later — see `LICENSE`.
