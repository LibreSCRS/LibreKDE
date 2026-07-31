# Arch packaging — librekde

`librekde` packages the KDE Plasma 6 integration: the smart-card plasmoid,
the credential-management window, the Purpose "Sign" share plugin, and the
`card:/` KIO worker.

LibreKDE is a **thin D-Bus client** of the LibreSCRS agent — it links no
LibreMiddleware target, so it depends on **`librelinux`** (`librescrs-agent`),
**not** on `librescrs-middleware`. It also links the agent project's Qt client
library, so it depends on **`librescrs-agent-client-qt`** as well; that package
is built from the agent library's own repository and its own dependencies are
Qt6 and libc only, so it pulls in no part of the middleware stack.

The `PKGBUILD` is **release-shaped** (fetches the `v$pkgver` GitHub tag) and
uses **independent SemVer** (`pkgver=0.1.0`) — 0.x until the D-Bus wire
surface freezes.

## What lands where

`cmake --install` (via ECM `KDE_INSTALL_*`) installs under `/usr`:

- the Plasma 6 plasmoid package
- the credential-management window: `bin/librescrs-credentials-kde` +
  `share/applications/org.librescrs.credentials.desktop`
- the Purpose "Sign" plugin + its AppStream metainfo
- the `card:/` KIO worker
- the smartcard AppStream metainfo (`org.librescrs.smartcard.metainfo.xml`)
- the ki18n gettext catalogues (see below)

`ki18n_install(po)` now fires: the `po/` dir carries `sr` + `sr@latin`
catalogues for two gettext domains — `librekde` (all C++ strings, incl. the
credential window) and `plasma_applet_org.librescrs.smartcard` (the plasmoid
QML) — installed as `share/locale/<lang>/LC_MESSAGES/<domain>.mo`.

The credential-management window's `.desktop` is `NoDisplay=true` (it is
launched from the plasmoid, not the menu), so it gets **no separate AppStream
component**: AppStream desktop-application entries are for user-launchable
apps, and a hidden helper is not one. The only AppStream metainfo shipped is
the plasmoid addon (`org.librescrs.smartcard`) and the Purpose plugin's.

## Release build (after the `v0.1.0` tag is pushed)

```sh
cd packaging/arch
updpkgsums      # fills in the real sha256sum
makepkg -si
```

## Local dogfood build (no remote, no tag — build from this checkout)

Override the source to your local working tree:

```sh
REPO="$(git rev-parse --show-toplevel)"
mkdir -p /tmp/lk-arch && cp packaging/arch/PKGBUILD /tmp/lk-arch/
cd /tmp/lk-arch
# Replace the multi-line release `source=(...)` array wholesale with a single
# local-git entry (a single-line `s#^source=.*#...#` would mangle the
# multi-line array, leaving a dangling URL line + `)`). `sha256sums` is a
# single line, so a plain `s#` substitution is correct there.
# A git source named exactly LibreKDE-$pkgver checks out to
# $srcdir/LibreKDE-$pkgver — matching the hardcoded `cd` lines.
sed -i \
  -e "/^source=(/,/^)/c\\source=(\"LibreKDE-\$pkgver::git+file://$REPO\")" \
  -e "s#^sha256sums=.*#sha256sums=('SKIP')#" \
  PKGBUILD
makepkg -si
```

> `librelinux` (`librescrs-agent`) and `librescrs-agent-client-qt` must both be
> installed first. The whole chain has **two roots, not one**:
>
>     librescrs-middleware  <=  librelinux  <=  librekde
>     librescrs-agent-client-qt  <=  librekde
