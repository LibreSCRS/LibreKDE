#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Runs INSIDE a fresh container. /pkg holds this repository's packages,
# /pkg-<Repo> the upstream ones.
set -uo pipefail
fail=0
check() { if [ "$2" -eq 0 ]; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }

if [ "${FAMILY:-deb}" = deb ]; then
  export DEBIAN_FRONTEND=noninteractive
  # The base container is not a machine. Ubuntu's image ships
  # /etc/dpkg/dpkg.cfg.d/excludes with path-exclude=/usr/share/locale/*/LC_MESSAGES/*.mo
  # (Debian's does not), so a package that carries translations installs without
  # them there. Asserting on disk under that configuration measures the image,
  # not the package, so the exclusion goes before anything is installed.
  rm -f /etc/dpkg/dpkg.cfg.d/excludes
  apt-get update -qq
  apt-get install -y --no-install-recommends \
      /pkg-LibreMiddleware/liblibrescrs5_*.deb \
      /pkg-LibreAgent/liblibrescrs-agentclient-qt5_*.deb /pkg/*.deb >/dev/null
  check "V1 install with upstreams" $?
  LIBGLOB="/usr/lib/*"
  installed_list() { dpkg-query -W -f='${Package}\n'; }
  files_of() { dpkg -L "$1" 2>/dev/null; }
  remove_ours() { apt-get purge -y $(installed_list | grep -E 'librescrs|liblibrescrs|librekde') >/dev/null; }
else
  dnf -y -q install /pkg-LibreMiddleware/librescrs-middleware-5*.rpm \
      /pkg-LibreAgent/librescrs-agent-client-qt-5*.rpm /pkg/*.rpm >/dev/null
  check "V1 install with upstreams" $?
  LIBGLOB="/usr/lib64"
  installed_list() { rpm -qa --qf '%{NAME}\n'; }
  files_of() { rpm -ql "$1" 2>/dev/null; }
  remove_ours() { dnf -y -q remove $(installed_list | grep -E '^(librescrs|liblibrescrs|librekde)') >/dev/null; }
fi

test -d /usr/share/plasma/plasmoids/org.librescrs.smartcard
check "V2 plasmoid package directory" $?
test -x /usr/bin/librescrs-credentials-kde
check "V2b credential window on PATH" $?
n=$(find $LIBGLOB/qt6/plugins/kf6/kio -name '*.so' 2>/dev/null | wc -l)
test "$n" -ge 1; check "V2c KIO worker installed (counted $n)" $?
n=$(find $LIBGLOB/qt6/plugins/kf6/purpose -name '*.so' 2>/dev/null | wc -l)
test "$n" -ge 1; check "V2d Purpose plugin installed (counted $n)" $?

# Translations are asserted as FILES ON A PATH, never by searching a binary:
# the Qt resource is compressed, so every .qm string is invisible to a scan of
# the executable, and `strings` cannot see Cyrillic in UTF-16 in any case.
n=$(find /usr/share/locale -name 'librekde.mo' 2>/dev/null | wc -l)
test "$n" -ge 1; check "V2e message catalogues installed (counted $n)" $?
find /usr/share/locale -name 'librekde.mo' | head

ours=$(installed_list | grep -E 'librekde' | sort -u)
: > /tmp/all.txt
for p in $ours; do files_of "$p" | while read -r f; do [ -f "$f" ] || [ -L "$f" ] && echo "$f"; done; done \
  | sort > /tmp/all.txt
uniq -d < /tmp/all.txt > /tmp/dupes.txt
n=$(wc -l < /tmp/all.txt); echo "PATH_COUNT=$n"
# An empty list has no duplicates either, so the disjointness claim would pass
# vacuously the moment the install step failed -- exactly when it must not.
test "$n" -gt 5; check "V-coexist the path list is not empty (counted $n)" $?
test ! -s /tmp/dupes.txt; check "V-coexist every installed path owned exactly once" $?
[ -s /tmp/dupes.txt ] && cat /tmp/dupes.txt

miss=0
for f in /usr/bin/librescrs-credentials-kde $(find $LIBGLOB/qt6/plugins/kf6 -name '*.so' 2>/dev/null); do
  [ -e "$f" ] || continue
  if ldd "$f" 2>/dev/null | grep -q 'not found'; then echo "  not found in $f"; miss=1; fi
done
test "$miss" -eq 0; check "V11 no unresolved shared-library dependency" $?

remove_ours
find /usr -iname '*librekde*' -o -iname '*org.librescrs.smartcard*' > /tmp/leftover.txt
test ! -s /tmp/leftover.txt; check "V9 nothing left under /usr after removal" $?
[ -s /tmp/leftover.txt ] && cat /tmp/leftover.txt
exit $fail
