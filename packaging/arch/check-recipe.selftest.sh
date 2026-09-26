#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Selftest for check-recipe.sh. The shapes the recipe (or the tree around it)
# gets wrong, plus the real recipe as a control. One case only applies to a
# repository whose recipe carries a FetchContent pin; where it does not, the
# file says so out loud and counts one case fewer, because a silently dropped
# case is indistinguishable from one that passed.
#
# Each case asserts three things, because two of them are not enough:
#   * the fixture actually differs from the control (a perturbation that
#     changed nothing passes for the wrong reason);
#   * the exit code is non-zero;
#   * the named arm appears in the output. An exit code alone cannot tell a
#     refusal that worked from a refusal that fired on something else.
#
# Every fixture is a throwaway git repository under /var/tmp -- never the
# working tree, and never /tmp, which is RAM on some development machines.
# The repository name the gate compares the source URL against is passed as
# REPO_NAME, derived here the way the gate derives it for the real tree, so a
# fixture directory's name cannot decide a case.
#
# The one git object-writing command runs with signing turned off for the
# invocation: a maintainer with commit.gpgSign=true set globally would
# otherwise be asked for a passphrase by pinentry, and the case would hang.
# shellcheck disable=SC2016  # the recipe's own $pkgver, as literal text
set -u

here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
subject="${CHECK_RECIPE:-$here/check-recipe.sh}"
control_recipe="$here/PKGBUILD"
root=$(CDPATH='' cd -- "$here/../.." && pwd)

gr=${GITHUB_REPOSITORY:-}
repo="${REPO_NAME:-${gr##*/}}"
if [ -z "$repo" ]; then
  origin=$(git -C "$root" remote get-url origin 2>/dev/null || true)
  origin=${origin%.git}; repo=${origin##*/}; repo=${repo##*:}
fi
[ -n "$repo" ] || repo=$(basename "$root")
export REPO_NAME="$repo"

work="${TMPDIR_SELFTEST:-/var/tmp/check-recipe-selftest.$$}"
rm -rf "$work"; mkdir -p "$work"
trap 'rm -rf "$work"' EXIT

fails=0
cases=0
red=0

fx() { printf '%s/%s/%s\n' "$work" "$1" "$repo"; }

# fixture <name> -> builds $(fx <name>) as a minimal repository
fixture() {
    local c="$1" d
    d=$(fx "$c")
    mkdir -p "$d/packaging/arch" "$d/packaging/rpm"
    cp "$control_recipe" "$d/packaging/arch/PKGBUILD"
    cp "$subject"        "$d/packaging/arch/check-recipe.sh"
    chmod +x "$d/packaging/arch/check-recipe.sh"
    cp "$root/VERSION" "$root/KEYS" "$d/"
    cp "$root"/packaging/rpm/*.spec "$d/packaging/rpm/"
    [ -f "$root/cmake/FetchQCBOR.cmake" ] && {
        mkdir -p "$d/cmake"
        cp "$root/cmake/FetchQCBOR.cmake" "$d/cmake/"
    }
    git -C "$d" init --quiet
    git -C "$d" add -A >/dev/null 2>&1
    git -C "$d" -c user.name=selftest -c user.email=selftest@invalid \
        -c commit.gpgsign=false \
        commit --quiet -m fixture >/dev/null 2>&1
}

run() {  # run <name> ; sets $out and $rc
    out=$(cd "$(fx "$1")" && bash packaging/arch/check-recipe.sh 2>&1)
    rc=$?
}

judge() {  # judge <name> <substring> -- after run
    local c="$1" want="$2"
    if [ "$rc" -eq 0 ]; then
        echo "CASE $c: expected a non-zero exit, got 0"
        printf '%s\n' "$out" | sed 's/^/    /'
        fails=$((fails + 1)); return
    fi
    case "$out" in
        *"$want"*) : ;;
        *) echo "CASE $c: exit was non-zero but no line mentions '$want'"
           printf '%s\n' "$out" | sed 's/^/    /'
           fails=$((fails + 1)) ;;
    esac
}

changed() {  # changed <name> -- did the perturbation touch anything the gate reads?
    local d f
    d=$(fx "$1")
    for f in packaging/arch/PKGBUILD VERSION KEYS; do
        cmp -s "$d/$f" "$root/$f" 2>/dev/null || return 0
    done
    for f in "$root"/packaging/rpm/*.spec; do
        cmp -s "$d/packaging/rpm/$(basename "$f")" "$f" || return 0
    done
    return 1
}

expect_red() {  # expect_red <name> <substring> -- a perturbation of a file
    local c="$1"
    cases=$((cases + 1))
    red=$((red + 1))
    if ! changed "$c"; then
        echo "CASE $c: the fixture is identical to the control -- the perturbation changed nothing"
        fails=$((fails + 1)); return
    fi
    run "$c"
    judge "$c" "$2"
}

recipe_of() { printf '%s/packaging/arch/PKGBUILD\n' "$(fx "$1")"; }

# 1 -- GitHub's auto-generated archive: its bytes are not ours to assert.
fixture auto_archive
sed -i 's#"git+https://github.com/LibreSCRS/\([A-Za-z]*\)\.git\#tag=\$pkgver?signed"#"$pkgname-$pkgver.tar.gz::https://github.com/LibreSCRS/\1/archive/refs/tags/$pkgver.tar.gz"#' \
    "$(recipe_of auto_archive)"
expect_red auto_archive "auto-generated archive"

# 2 -- the shape this recipe had before: the release tarball under a SKIP
#      checksum, which says nothing about the bytes until someone fills it in
#      after the release, and nothing at all if nobody does.
fixture release_tarball
sed -i 's#"git+https://github.com/LibreSCRS/\([A-Za-z]*\)\.git\#tag=\$pkgver?signed"#"$pkgname-$pkgver.tar.gz::https://github.com/LibreSCRS/\1/releases/download/$pkgver/librekde_$pkgver.orig.tar.gz"#' \
    "$(recipe_of release_tarball)"
expect_red release_tarball "fetches a release asset"

# 3 -- a v-prefixed tag, which no tag in this stack is.
fixture v_prefixed_tag
sed -i 's#\#tag=\$pkgver#\#tag=v$pkgver#' "$(recipe_of v_prefixed_tag)"
expect_red v_prefixed_tag "v-prefixed"

# 4 -- no ref at all: makepkg builds the default branch.
fixture no_ref
sed -i 's#\#tag=\$pkgver?signed#?signed#' "$(recipe_of no_ref)"
expect_red no_ref "names no ref"

# 5 -- a branch instead of the tag.
fixture branch_ref
sed -i 's#\#tag=\$pkgver#\#branch=main#' "$(recipe_of branch_ref)"
expect_red branch_ref "not to the tag"

# 6 -- the tag without ?signed: makepkg never checks the signature.
fixture unsigned
sed -i 's#?signed"#"#' "$(recipe_of unsigned)"
expect_red unsigned "not marked ?signed"

# 7 -- a sibling repository's source. These recipes are near-copies of one
#      another, so this is what a careless copy produces.
fixture sibling_repo
sed -i "s#github.com/LibreSCRS/$repo\.git#github.com/LibreSCRS/NotThisRepo.git#" \
    "$(recipe_of sibling_repo)"
expect_red sibling_repo "while this repository is"

# 8 -- validpgpkeys names another key.
fixture wrong_key
sed -i "s/^validpgpkeys=('[0-9A-F]\{40\}')/validpgpkeys=('0123456789ABCDEF0123456789ABCDEF01234567')/" \
    "$(recipe_of wrong_key)"
expect_red wrong_key "arm1b: validpgpkeys names"

# 9 -- no validpgpkeys: any key the builder trusts would do.
fixture no_key
sed -i '/^validpgpkeys=/d' "$(recipe_of no_key)"
expect_red no_key "no validpgpkeys entry"

# 10 -- pkgver disagrees with VERSION.
fixture pkgver_drift
sed -i 's/^pkgver=.*/pkgver=4.2.0/' "$(recipe_of pkgver_drift)"
expect_red pkgver_drift "arm2"

# 11 -- VERSION missing entirely. A missing input is a failure, not a skip.
fixture no_version
rm -f "$(fx no_version)/VERSION"
expect_red no_version "arm2"

# 12 -- the RPM spec's Version: disagrees with VERSION.
fixture spec_drift
sed -i 's/^Version:\([[:space:]]*\).*/Version:\14.2.0/' "$(fx spec_drift)"/packaging/rpm/*.spec
expect_red spec_drift "arm2c"

# 13 -- the vacuum: source=() renamed so the pattern matches nothing.
fixture vacuum_source
sed -i 's/^source=(/sources=(/' "$(recipe_of vacuum_source)"
expect_red vacuum_source "vacuum"

# 14 -- a checksum on the git source, which makepkg refuses.
fixture git_with_sum
sed -i "s/^sha256sums=('SKIP')/sha256sums=('$(printf '%064d' 0)')/" "$(recipe_of git_with_sum)"
expect_red git_with_sum "arm4: the git source"

# 15 -- a downloaded source added with SKIP: a download needs its real sum.
fixture download_skip
sed -i 's#^source=(#source=(\n    "extra.tar.gz::https://example.invalid/extra.tar.gz"#' "$(recipe_of download_skip)"
sed -i "s/^sha256sums=('SKIP')/sha256sums=('SKIP' 'SKIP')/" "$(recipe_of download_skip)"
expect_red download_skip "instead of its sha256"

# 16 -- one checksum short: makepkg pairs sources and sums by position.
fixture sums_short
sed -i 's#^source=(#source=(\n    "extra.tar.gz::https://example.invalid/extra.tar.gz"#' "$(recipe_of sums_short)"
expect_red sums_short "pairs them by position"

# 17 -- a submodule gitlink the recipe does not pin. The perturbation is in the
#       INDEX, invisible in the recipe text.
fixture gitlink_drift
git -C "$(fx gitlink_drift)" update-index --add \
    --cacheinfo 160000,1111111111111111111111111111111111111111,thirdparty/not-pinned \
    >/dev/null 2>&1
cases=$((cases + 1)); red=$((red + 1))
if [ -z "$(git -C "$(fx gitlink_drift)" ls-files -s -- thirdparty/not-pinned)" ]; then
    echo "CASE gitlink_drift: the gitlink was not written -- the perturbation changed nothing"
    fails=$((fails + 1))
else
    run gitlink_drift
    judge gitlink_drift "arm3: submodule"
fi

# 18 -- the FetchContent pin drifts from the cmake module the build fetches
#       with. Only applies where the recipe carries one.
if grep -q '^_qcbor_commit=' "$control_recipe"; then
    fixture fetchcontent_drift
    sed -i -E 's/^([[:space:]]*GIT_TAG[[:space:]]+)[0-9a-f]{40}/\12222222222222222222222222222222222222222/' \
        "$(fx fetchcontent_drift)/cmake/FetchQCBOR.cmake"
    cases=$((cases + 1)); red=$((red + 1))
    if cmp -s "$(fx fetchcontent_drift)/cmake/FetchQCBOR.cmake" "$root/cmake/FetchQCBOR.cmake"; then
        echo "CASE fetchcontent_drift: the pin was not rewritten -- the perturbation changed nothing"
        fails=$((fails + 1))
    else
        run fetchcontent_drift
        judge fetchcontent_drift "arm3b: QCBOR pin drift"
    fi
else
    echo "CASE fetchcontent_drift: not applicable -- this recipe carries no _qcbor_commit"
fi

# 19 -- control: the real recipe, untouched, passes, and every arm says what
#       it measured. A silent arm is the failure mode this file exists for.
fixture control
cases=$((cases + 1))
run control
if [ "$rc" -ne 0 ]; then
    echo "CASE control: the committed recipe does not pass its own gate (rc=$rc)"
    printf '%s\n' "$out" | sed 's/^/    /'; fails=$((fails + 1))
fi
for arm in 'arm1:' 'arm1b:' 'arm2:' 'arm2c:' 'arm3:' 'arm3b:' 'arm4:'; do
    case "$out" in
        *"$arm"*) : ;;
        *) echo "CASE control: $arm said nothing about itself -- a silent arm is a vacuum"
           fails=$((fails + 1)) ;;
    esac
done

if [ "$fails" -eq 0 ]; then
    echo "check-recipe selftest: all $cases cases passed"
    printf 'selftest: %s cases, %s red-proved\n' "$cases" "$red"
    exit 0
fi
echo "check-recipe selftest: $fails of $cases case(s) failed"
printf 'selftest: %s cases, %s red-proved\n' "$cases" "$red"
exit 1
