#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# check-recipe.sh
#
# The Arch recipe must build THIS repository's signed release tag, and must say
# true things about it. Every arm prints what it measured, because a silent
# arm is a vacuum and not a pass.
#
#   1  source= holds exactly one git source of THIS repository, pinned to the
#      tag `$pkgver` (unprefixed, like every tag in this stack) and marked
#      ?signed; no archive download of any kind. GitHub's auto-generated
#      archive's bytes are not ours to assert, and a tarball can only be
#      checksummed after the release exists -- the signed tag is the same
#      claim, checkable before and after.
#   1b validpgpkeys names exactly the primary fingerprint(s) KEYS carries:
#      makepkg matches the PRIMARY key of the signature, and a recipe pinning
#      another key accepts a tag nobody in this project signed.
#   2  pkgver equals the first line of VERSION, and so does the RPM spec's
#      Version:. pkgver only labels the package; the installed CMake version
#      file is generated from VERSION, so a bump that misses one of them ships
#      a package whose own metadata disagrees.
#   3  every submodule gitlink is pinned verbatim in the recipe; and a
#      FetchContent pin carried by the recipe equals the pin in the cmake
#      module the build would otherwise fetch with.
#   4  sha256sums has one entry per source: SKIP for a git source (makepkg has
#      no archive to checksum there; ?signed is the check), a real 64-hex sum
#      for anything downloaded. The rule holds before and after the tag, so it
#      needs neither tags in the clone nor the network.
#
# The repository is REPO_NAME, else the repository part of GITHUB_REPOSITORY,
# else the last path segment of the `origin` remote, else the directory name.
#
# Threat model. This reads the recipe as TEXT rather than sourcing it, so it
# guards against the honest regression: someone edits source=, bumps a version
# or rotates a key in the shapes this repository actually writes, and gets one
# of them wrong. It does not resist a recipe written to conceal intent: a URL
# assembled from variables, an architecture array (source_x86_64=()), a
# source=( or its closing ) not at column 0, or a pin that appears only inside
# a comment. Code review, not this gate, is what catches a recipe written to
# mislead.
#
# Exit codes: 0 every arm passed, 1 an arm failed, 2 cannot judge (no recipe,
# no gpg to read KEYS).
set -u

case "${1:-}" in
  '') ;;
  *) echo "usage: check-recipe.sh" >&2; exit 2 ;;
esac

here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH='' cd -- "$here/../.." && pwd)
recipe="$here/PKGBUILD"

rc=0
note() { printf '%-6s %s\n' "$1" "$2"; }
bad()  { note FAIL "$1"; rc=1; }

[ -f "$recipe" ] || { note FAIL "no PKGBUILD next to this script ($recipe)"; exit 2; }

gr=${GITHUB_REPOSITORY:-}
reponame="${REPO_NAME:-${gr##*/}}"
if [ -z "$reponame" ]; then
  origin=$(git -C "$root" remote get-url origin 2>/dev/null || true)
  origin=${origin%.git}; reponame=${origin##*/}; reponame=${reponame##*:}
fi
[ -n "$reponame" ] || reponame=$(basename "$root")

pkgver=$(sed -nE 's/^pkgver=([^[:space:]#]+).*/\1/p' "$recipe" | head -1)
[ -n "$pkgver" ] || bad "no pkgver= in $recipe"

# entries <array-name> -> the quoted entries of a column-0 name=( ... ) array,
# single-line or multi-line; a trailing comment is dropped
entries() {
  awk -v n="$1" '$0 ~ "^" n "=\\(" {on=1} on {print; if ($0 ~ /\)/) exit}' "$recipe" \
    | sed -E "s/[[:space:]]#[^\"']*\$//" \
    | grep -oE "\"[^\"]*\"|'[^']*'" | tr -d "\"'"
}

# The spellings of the tag ref this recipe may use, as literal text.
# shellcheck disable=SC2016
tag_refs=('tag=$pkgver' 'tag=${pkgver}')

# --- arm 1 -----------------------------------------------------------------
mapfile -t srcs < <(entries source)
n=${#srcs[@]}
if ! grep -q '^source=(' "$recipe"; then
  bad "arm1: no 'source=(' ... ')' array in the recipe -- the pattern matches nothing, which is a vacuum and not a pass"
elif [ "$n" -eq 0 ]; then
  bad "arm1: the source=() array holds no entries"
else
  own=0
  for e in "${srcs[@]}"; do
    url=${e#*::}
    case "$url" in
      *archive/refs/tags*)
        bad "arm1: fetches GitHub's auto-generated archive, whose bytes this project does not produce: $url" ;;
      *releases/download/*)
        bad "arm1: fetches a release asset, which can only be checksummed once the release exists -- build the signed tag: $url" ;;
    esac
    case "$url" in
      git+https://github.com/LibreSCRS/*)
        rest=${url#git+https://github.com/LibreSCRS/}
        erepo=${rest%%[#?]*}; erepo=${erepo%.git}
        if [ "$erepo" != "$reponame" ]; then
          bad "arm1: the git source is LibreSCRS/$erepo while this repository is $reponame -- a recipe copied between siblings packages the other one's sources"
          continue
        fi
        own=$((own + 1))
        frag=""
        case "$url" in *'#'*) frag=${url#*#} ;; esac
        ref=${frag%%\?*}
        if [ "$ref" = "${tag_refs[0]}" ] || [ "$ref" = "${tag_refs[1]}" ]; then
          :
        elif [ -z "$ref" ]; then
          bad "arm1: the git source names no ref, so makepkg builds whatever the default branch holds: $url"
        elif [ "${ref#tag=v}" != "$ref" ]; then
          bad "arm1: asks for a v-prefixed tag; every tag this project publishes is unprefixed: $url"
        else
          bad "arm1: the git source is pinned to '$ref', not to the tag \$pkgver: $url"
        fi
        case "$url" in
          *'?signed'*) : ;;
          *) bad "arm1: the git source is not marked ?signed, so makepkg never checks the tag's signature: $url" ;;
        esac ;;
    esac
  done
  printf 'arm1: %d source entr%s, %d the signed tag of LibreSCRS/%s\n' \
    "$n" "$([ "$n" -eq 1 ] && echo y || echo ies)" "$own" "$reponame"
  [ "$own" -eq 1 ] || bad "arm1: expected exactly one git source of this repository, found $own"
fi

# --- arm 1b ----------------------------------------------------------------
keys="$root/KEYS"
if ! command -v gpg >/dev/null 2>&1; then
  note FAIL "arm1b: gpg is not on PATH -- the key in KEYS cannot be read"; exit 2
elif [ ! -f "$keys" ]; then
  bad "arm1b: KEYS is missing -- validpgpkeys cannot be shown to name the release key"
else
  gh=$(mktemp -d)
  mapfile -t want < <(GNUPGHOME=$gh gpg --batch --show-keys --with-colons "$keys" 2>/dev/null \
                        | awk -F: '$1=="pub"{p=1; next} $1=="fpr" && p {print $10} {p=0}' | sort -u)
  rm -rf "$gh"
  mapfile -t have < <(entries validpgpkeys | tr '[:lower:]' '[:upper:]' | sort -u)
  if [ "${#want[@]}" -eq 0 ]; then
    bad "arm1b: KEYS holds no primary key gpg can read -- nothing to compare against"
  elif [ "${#have[@]}" -eq 0 ]; then
    bad "arm1b: no validpgpkeys entry -- makepkg would accept a tag signed by any key it trusts"
  elif [ "${want[*]}" != "${have[*]}" ]; then
    bad "arm1b: validpgpkeys names ${have[*]} but KEYS carries ${want[*]}"
  else
    printf 'arm1b: validpgpkeys names the primary key KEYS carries (%s)\n' "${want[*]}"
  fi
fi

# --- arm 2 -----------------------------------------------------------------
vf="$root/VERSION"
declared=""
if [ ! -f "$vf" ]; then
  bad "arm2: $vf is missing -- pkgver cannot be shown to agree with anything"
else
  declared=$(sed -n '1p' "$vf" | tr -d '[:space:]'); declared=${declared#v}
  if [ -z "$declared" ]; then
    bad "arm2: first line of VERSION is empty"
  elif [ "$declared" != "$pkgver" ]; then
    bad "arm2: version drift -- VERSION says $declared, the recipe says pkgver=$pkgver"
  else
    printf 'arm2: pkgver=%s equals the first line of VERSION\n' "$pkgver"
  fi
fi

spec="$root/packaging/rpm/librekde.spec"
if [ ! -f "$spec" ]; then
  printf 'arm2c: no packaging/rpm/librekde.spec -- nothing to compare against VERSION\n'
else
  specver=$(sed -nE 's/^Version:[[:space:]]+([^[:space:]]+).*/\1/p' "$spec" | head -1)
  if [ -z "$specver" ]; then
    bad "arm2c: no 'Version:' line in $spec"
  elif [ "$specver" != "$declared" ]; then
    bad "arm2c: version drift -- VERSION says $declared, $spec says Version: $specver"
  else
    printf 'arm2c: %s Version: %s matches VERSION\n' "$(basename "$spec")" "$specver"
  fi
fi

# --- arm 3 -----------------------------------------------------------------
# Read from the index, not from HEAD: the gate judges the tree it is run over.
gl=0; ok=0
while read -r mode sha _stage path; do
  [ "$mode" = 160000 ] || continue
  gl=$((gl + 1))
  if grep -q "$sha" "$recipe"; then ok=$((ok + 1))
  else bad "arm3: submodule $path is pinned at $sha, which appears nowhere in the recipe"; fi
done < <(git -C "$root" ls-files -s 2>/dev/null)
printf 'arm3: %d submodule gitlink(s), %d pinned in the recipe\n' "$gl" "$ok"

fm="$root/cmake/FetchQCBOR.cmake"
rp=$(sed -nE 's/^_qcbor_commit=([0-9a-f]{40}).*/\1/p' "$recipe" | head -1)
if [ -n "$rp" ] && [ ! -f "$fm" ]; then
  bad "arm3b: the recipe pins _qcbor_commit=$rp but $fm is missing -- nothing to keep it in lockstep with"
elif [ -n "$rp" ]; then
  mapfile -t decl < <(sed -nE 's/^[[:space:]]*GIT_TAG[[:space:]]+([0-9a-f]{40})[[:space:]]*$/\1/p' "$fm")
  if [ "${#decl[@]}" -ne 1 ]; then
    bad "arm3b: cmake/FetchQCBOR.cmake must hold exactly one 'GIT_TAG <40-hex>' line; found ${#decl[@]}"
  elif [ "${decl[0]}" != "$rp" ]; then
    bad "arm3b: QCBOR pin drift -- cmake says ${decl[0]}, the recipe says $rp"
  else
    printf 'arm3b: QCBOR pin %s matches cmake/FetchQCBOR.cmake\n' "$rp"
  fi
else
  printf 'arm3b: the recipe carries no _qcbor_commit -- nothing to keep in lockstep\n'
fi

# --- arm 4 -----------------------------------------------------------------
mapfile -t sums < <(entries sha256sums)
if [ "${#sums[@]}" -eq 0 ]; then
  bad "arm4: no sha256sums entries found -- the pattern matches nothing, which is a vacuum and not a pass"
elif [ "${#sums[@]}" -ne "$n" ]; then
  bad "arm4: ${#sums[@]} sha256sums entries for $n source entries -- makepkg pairs them by position"
else
  n_git=0; n_real=0
  for i in "${!srcs[@]}"; do
    url=${srcs[$i]#*::}; v=${sums[$i]}
    case "$url" in
      git+*)
        n_git=$((n_git + 1))
        [ "$v" = SKIP ] || bad "arm4: the git source $url carries '$v' -- makepkg checks a git source by its signature and refuses a sum there" ;;
      *)
        if [[ "$v" =~ ^[0-9a-f]{64}$ ]]; then n_real=$((n_real + 1))
        else bad "arm4: $url is a download and carries '$v' instead of its sha256"; fi ;;
    esac
  done
  printf 'arm4: %d git source(s) with SKIP, %d download(s) with a real sha256\n' "$n_git" "$n_real"
fi

echo "check-recipe: $([ $rc -eq 0 ] && echo GREEN || echo RED)"
exit "$rc"
