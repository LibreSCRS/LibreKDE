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

The `PKGBUILD` is **release-shaped**: it builds the signed release tag of this
repository (`git+https://…#tag=$pkgver?signed`), and makepkg checks the tag's
signature against `validpgpkeys` -- the release key's primary fingerprint, the
one `KEYS` carries -- before it builds anything.
`pkgver` is the first line of the repository's `VERSION` file: this component
no longer carries its own 0.x SemVer and is released in lockstep with the
rest of the stack.

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

## Release build (after the `5.0.0` tag is published)

```sh
gpg --import KEYS   # once, from the repository root: makepkg verifies the tag with it
cd packaging/arch
makepkg -si
```

A git source has nothing to checksum, so `sha256sums` stays `SKIP` before and
after the tag: the signature is the check. `packaging/arch/check-recipe.sh`
holds the source, the tag, the key and the version to the tree on every push.

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
# A git source named exactly LibreKDE checks out to $srcdir/LibreKDE --
# matching the hardcoded `cd` lines. No #tag and no ?signed: this builds the
# working tree's HEAD, which carries no release signature.
sed -i \
  -e "/^source=(/,/^)/c\\source=(\"LibreKDE::git+file://$REPO\")" \
  -e "s#^sha256sums=.*#sha256sums=('SKIP')#" \
  PKGBUILD
makepkg -si
```

> `librelinux` (`librescrs-agent`) and `librescrs-agent-client-qt` must both be
> installed first. The whole chain has **two roots, not one**:
>
>     librescrs-middleware  <=  librelinux  <=  librekde
>     librescrs-agent-client-qt  <=  librekde
