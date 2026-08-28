#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0

"""Check the shipped translation catalogs against the strings in the sources.

`msgfmt --check` validates one catalog in isolation. It cannot see the two
failures that actually reach users: a string that gained a call site and never
reached the catalogs (English text in a translated session), and a translation
whose last call site went away (dead weight that hides the loss of a real
entry). Both need the msgid set the sources produce today, so this regenerates
the `.pot` files from the sources and diffs the msgid sets against every
catalog.

The comparison key is `msgctxt` + `msgid` + `msgid_plural`, parsed rather than
grepped: most entries here carry a context, and several msgids are split over
several quoted lines, so `grep '^msgid '` both over- and under-counts.

`Messages.sh` is written for KDE's scripty environment: it consumes `$XGETTEXT`
and `$podir` and cannot be run on its own. This supplies both, with a keyword
list covering the whole ki18n family. A call spelled with an entry point that
is NOT on that list would extract to nothing and read as green here, so the
sources are also scanned for i18n-looking entry points and an unknown one is a
failure rather than a silent gap.

An entry that legitimately has no call site to find -- a message key an agent
supplies at run time, which no static extractor can see -- carries a
`# no-extract: <why>` translator comment. That marker is the only way an entry
is allowed to exist without a source of its own.

Exit status: 0 all catalogs reconcile, 1 discrepancies found, 2 tooling failure.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# The ki18n entry points, in the argument form xgettext needs. Keep the two
# halves in step: KEYWORD_SPECS drives extraction, KNOWN_KEYWORDS drives the
# scan that catches a call this list does not cover.
KEYWORD_SPECS = [
    "i18n:1", "i18nc:1c,2", "i18np:1,2", "i18ncp:1c,2,3",
    "i18nd:2", "i18ndc:2c,3", "i18ndp:2,3", "i18ndcp:2c,3,4",
    "xi18n:1", "xi18nc:1c,2", "xi18np:1,2", "xi18ncp:1c,2,3",
    "xi18nd:2", "xi18ndc:2c,3", "xi18ndp:2,3", "xi18ndcp:2c,3,4",
    "ki18n:1", "ki18nc:1c,2", "ki18np:1,2", "ki18ncp:1c,2,3",
    "ki18nd:2", "ki18ndc:2c,3", "ki18ndp:2,3", "ki18ndcp:2c,3,4",
    "kxi18n:1", "kxi18nc:1c,2", "kxi18np:1,2", "kxi18ncp:1c,2,3",
    "kxi18nd:2", "kxi18ndc:2c,3", "kxi18ndp:2,3", "kxi18ndcp:2c,3,4",
    "tr2i18n:1", "tr2xi18n:1", "I18N_NOOP:1", "I18NC_NOOP:1c,2",
]
KNOWN_KEYWORDS = {spec.split(":", 1)[0] for spec in KEYWORD_SPECS}

# Anything that looks like a call to an i18n entry point. Deliberately loose:
# a name this catches but KNOWN_KEYWORDS does not hold is the finding.
I18N_CALL = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*i18n[A-Za-z0-9_]*)\s*\(")

SOURCE_DIRS = ("components", "shared")
SOURCE_SUFFIXES = (".cpp", ".h", ".qml")

ESCAPES = {
    "n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\",
    "a": "\a", "b": "\b", "f": "\f", "v": "\v", "0": "\0",
}


def unquote(line):
    """Contents of the quoted run on `line`, with C escapes resolved."""
    start = line.index('"')
    end = line.rindex('"')
    if end <= start:
        raise ValueError("unterminated string: %r" % line)
    body = line[start + 1:end]
    out = []
    i = 0
    while i < len(body):
        char = body[i]
        if char == "\\":
            i += 1
            out.append(ESCAPES.get(body[i], body[i]))
        else:
            out.append(char)
        i += 1
    return "".join(out)


def parse_po(path):
    """Every entry in `path` as {key, comments}, header and obsoletes dropped."""
    entries = []
    current = {}
    comments = []
    field = None

    def flush():
        nonlocal current, comments, field
        if current.get("msgid") is not None:
            is_header = current["msgid"] == "" and "msgctxt" not in current
            if not is_header:
                entries.append({
                    "key": (current.get("msgctxt"), current["msgid"],
                            current.get("msgid_plural")),
                    "comments": comments,
                })
        current, comments, field = {}, [], None

    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            flush()
            continue
        if line.startswith("#~"):
            continue  # obsolete entry, already retired
        if line.startswith("#"):
            comments.append(line)
            continue
        for keyword in ("msgctxt", "msgid_plural", "msgid", "msgstr"):
            if line.startswith(keyword + " "):
                field = keyword
                current[field] = unquote(line)
                break
        else:
            if line.startswith("msgstr["):
                field = "msgstr"
                current[field] = current.get(field, "") + unquote(line)
            elif line.startswith('"'):
                if field is None:
                    raise ValueError("%s: continuation with no field: %r" % (path, raw))
                current[field] = current.get(field, "") + unquote(line)
            else:
                raise ValueError("%s: unparsed line: %r" % (path, raw))
    flush()
    return entries


def describe(key):
    context, msgid, plural = key
    text = msgid if plural is None else "%s / %s" % (msgid, plural)
    return "[%s] %s" % (context, text) if context is not None else text


def refs_of(entry):
    """The SET of individual `#:` source-location references on `entry`.

    A stale or missing reference does not cost a translator anything -- the
    string still translates -- but it does cost the NEXT person who trusts
    the comment to find the call site. `msgfmt --check` and the msgid-set
    diff above cannot see this: both are blind to a comment that simply
    points at the wrong line, or at no line at all.

    A single `#: ` comment line can carry more than one `file:line` token
    separated by whitespace, and the SAME reference set reflows across a
    different number of comment lines depending on who last touched the
    file -- msgmerge, Lokalize, Poedit, Weblate, and different gettext
    versions all wrap differently. Comparing raw comment LINES would flag
    that reflow as drift; split each line on whitespace and compare the
    reference set instead.
    """
    refs = set()
    for comment in entry["comments"]:
        if comment.startswith("#:"):
            refs.update(comment[2:].split())
    return refs


def obsolete_msgids(path):
    """A human-readable name for every `#~`-obsoleted entry in `path`.

    The actual check is simpler than this parsing: "any `#~` line at all is
    a finding," decided by the caller from a plain substring scan. This is
    only for naming which entry it was, best-effort -- a `#~ msgid "..."`
    line names itself; anything else obsoleted (a stray `#~ msgctxt` or
    `#~ msgstr` without its own msgid on the same line, which real gettext
    tools do not produce but a hand-edit could) falls back to the raw line
    so the finding is never silently unnamed.
    """
    found = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line.startswith("#~"):
            continue
        content = line[2:].strip()
        if content.startswith("msgid"):
            try:
                found.append(unquote(content))
            except ValueError:
                found.append(content)
        elif content.startswith("msgid_plural") or content.startswith("msgstr"):
            continue  # named by the msgid line already reported alongside it
        else:
            found.append(line)
    return found


def report_msgid_bugs_to(path):
    """The catalog header's `Report-Msgid-Bugs-To:` value, or "" if blank/absent."""
    header = parse_po_header(path)
    match = re.search(r"Report-Msgid-Bugs-To:\s*(.*?)\n", header)
    return match.group(1).strip() if match else ""


def parse_po_header(path):
    """The concatenated msgstr of the header entry (empty msgid, no msgctxt)."""
    header = ""
    in_header = False
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            if in_header:
                break
            continue
        if line.startswith("#"):
            continue
        if line.startswith("msgid "):
            in_header = unquote(line) == ""
            continue
        if not in_header:
            continue
        if line.startswith("msgstr ") or line.startswith('"'):
            header += unquote(line)
    return header


def scan_entry_points(root):
    """i18n-looking entry points used in the sources, mapped to their files."""
    found = {}
    for directory in SOURCE_DIRS:
        base = root / directory
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            for name in I18N_CALL.findall(path.read_text(encoding="utf-8")):
                found.setdefault(name, path.relative_to(root))
    return found


def existing_bugs_address(po_root):
    """The `Report-Msgid-Bugs-To` value the shipped catalogs already carry.

    Every catalog carries the same address today; read it from whichever one
    exists rather than hardcoding it a second time here. If none carry a
    value (or none exist yet), extraction proceeds without the flag and the
    header-empty check below catches the gap on the catalog side instead.
    """
    for path in sorted(po_root.rglob("*.po")):
        value = report_msgid_bugs_to(path)
        if value:
            return value
    return ""


def extract_pots(root, podir):
    """Run Messages.sh with the environment KDE's scripty would give it."""
    bugs_address = existing_bugs_address(root / "po")
    xgettext = " ".join(
        ["xgettext", "--from-code=UTF-8", "-C", "--kde", "-ci18n"]
        + (["--msgid-bugs-address=" + bugs_address] if bugs_address else [])
        + ["-k" + spec for spec in KEYWORD_SPECS]
    )
    env = dict(os.environ, XGETTEXT=xgettext, podir=str(podir))
    subprocess.run(["sh", "./Messages.sh"], cwd=root, env=env, check=True)
    return sorted(podir.glob("*.pot"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verbose", action="store_true",
                        help="list the reconciled key count per catalog")
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    po_root = root / "po"
    problems = []

    unknown = {name: where for name, where in scan_entry_points(root).items()
               if name not in KNOWN_KEYWORDS}
    for name, where in sorted(unknown.items()):
        problems.append("%s() in %s is not an extraction keyword — its strings "
                        "reach no catalog" % (name, where))

    with tempfile.TemporaryDirectory() as tmp:
        try:
            pots = extract_pots(root, Path(tmp))
        except subprocess.CalledProcessError as exc:
            print("extraction failed (exit %d)" % exc.returncode, file=sys.stderr)
            return 2
        except OSError as exc:
            # A missing xgettext (or sh) is a tooling failure, not a catalog
            # finding: report it as one instead of ending in a traceback.
            print("extraction could not run: %s" % exc, file=sys.stderr)
            return 2
        if not pots:
            print("extraction produced no .pot files", file=sys.stderr)
            return 2

        languages = sorted(path.name for path in po_root.iterdir() if path.is_dir())
        for pot in pots:
            domain = pot.stem
            source_entries = {entry["key"]: entry for entry in parse_po(pot)}
            source_keys = set(source_entries)
            per_language = {}

            for language in languages:
                catalog = po_root / language / (domain + ".po")
                if not catalog.exists():
                    problems.append("%s/%s.po is missing" % (language, domain))
                    continue

                try:
                    check = subprocess.run(["msgfmt", "--check", "-o", os.devnull, str(catalog)],
                                           capture_output=True, text=True)
                except OSError as exc:
                    print("msgfmt could not run: %s" % exc, file=sys.stderr)
                    return 2
                if check.returncode != 0:
                    problems.append("msgfmt --check rejected %s/%s.po:\n%s"
                                    % (language, domain, check.stderr.rstrip()))

                entries = parse_po(catalog)
                catalog_entries = {entry["key"]: entry for entry in entries}
                catalog_keys = set(catalog_entries)
                exempt = {entry["key"] for entry in entries
                          if any(c.startswith("# no-extract:") for c in entry["comments"])}
                per_language[language] = catalog_keys

                for key in sorted(source_keys - catalog_keys, key=repr):
                    problems.append("%s/%s.po: no entry for a string the sources "
                                    "produce: %s" % (language, domain, describe(key)))
                for key in sorted(catalog_keys - source_keys - exempt, key=repr):
                    problems.append("%s/%s.po: entry has no call site left in the "
                                    "sources: %s" % (language, domain, describe(key)))
                # Every key present on both sides has a real call site today,
                # so its `#:` comment has one right answer. Compare it, not
                # just the msgid set: a reference can drift line-by-line
                # (the source moved) or go missing outright (never carried
                # over from an earlier hand-edit) without the checks above
                # noticing either way.
                for key in sorted(source_keys & catalog_keys, key=repr):
                    source_refs = refs_of(source_entries[key])
                    catalog_refs = refs_of(catalog_entries[key])
                    if catalog_refs != source_refs:
                        problems.append(
                            "%s/%s.po: stale source reference for %s (catalog: %s; "
                            "sources: %s)"
                            % (language, domain, describe(key),
                               ", ".join(sorted(catalog_refs)) or "none",
                               ", ".join(sorted(source_refs)) or "none"))

                # `parse_po` deliberately skips `#~` lines (they are not live
                # entries), which is exactly why an obsoleted entry is invisible
                # to every check above: its key just vanishes from `catalog_keys`
                # as if it had never existed, msgid set and reference set alike.
                # msgmerge marks an entry obsolete whenever ITS OWN regeneration
                # cannot find a source key for it -- which is precisely what
                # happens to a `# no-extract:` entry, since by definition no
                # extractor can find one. A shipped catalog carrying `#~` is
                # always a mistake here: either a real translation got dropped
                # (msgmerge did not know about the no-extract convention), or
                # dead weight was committed instead of deleted.
                for obsolete in obsolete_msgids(catalog):
                    problems.append("%s/%s.po: obsolete entry `#~` in a shipped "
                                    "catalog: %s" % (language, domain, obsolete))

                bugs_to = report_msgid_bugs_to(catalog)
                if not bugs_to:
                    problems.append("%s/%s.po: Report-Msgid-Bugs-To header is "
                                    "empty" % (language, domain))
                if args.verbose:
                    print("%-8s %-40s %3d entries (%d exempt)"
                          % (language, domain, len(catalog_keys), len(exempt)))

            reference = None
            for language, keys in sorted(per_language.items()):
                if reference is None:
                    reference = (language, keys)
                    continue
                for key in sorted(reference[1] ^ keys, key=repr):
                    problems.append("%s: %s and %s do not carry the same entries: %s"
                                    % (domain, reference[0], language, describe(key)))

    if problems:
        print("Catalogs do not reconcile with the sources:\n", file=sys.stderr)
        for problem in problems:
            print("  - %s" % problem, file=sys.stderr)
        print("\n%d problem(s)." % len(problems), file=sys.stderr)
        return 1

    print("Catalogs reconcile with the sources.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
