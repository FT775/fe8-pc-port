#!/usr/bin/env python3
"""Bring the naming stack's names into a port tree, as a scripted rename.

    python3 port/tools/apply_names.py apply  [--root DIR] [--map FILE] [--dry-run]
                                             [--drop-refused] [--skip-hold] [--skip-offlimits]
                                             [--allow-clash NAME ...] [--report FILE]
    python3 port/tools/apply_names.py status [--root DIR] [--map FILE]
    python3 port/tools/apply_names.py verify-stack [--repo DIR] [--map FILE] [--base REV] [--tip REV]
    python3 port/tools/apply_names.py crosscheck   [--root DIR] [--map FILE] [--base REV] [--tip REV]
    python3 port/tools/apply_names.py check-elf --old OLD.elf --new NEW.elf [--names-only] [--map FILE]
    python3 port/tools/apply_names.py lookup NAME ...
    python3 port/tools/apply_names.py derive [--repo DIR] [--base REV] [--tip REV]
                                             [--elf GBA_ELF --mapfile GBA_MAP] [-o FILE]

The naming stack (branch name-final11 up to its last rename commit 257b1607,
worktree ~/fe8-names11; names10 read name-medium-accept) renames game symbols
on top of the decomp; the port branches keep the older names. A git merge of
the two conflicts in nearly every file the port touched, so the port takes the
names by this script instead (course correction (c),
port-plan/pending/names10.md and names11.md).

The map (port/tools/names_map.tsv, made by `derive`) is the naming stack's own
renames, read back from its commits: the stack renamed with whole-word
substitutions only (~/apply_renames.sh), so every file it changed has the
same words in the same order, and the words that differ pair an old name with
its new one. `derive` refuses when that is not so (a stack commit that changes
more than names), when one old name has two new names, when two old names get
one new name, or when a new name is also an old one (a chain the stack's
commits would compose: the map holds the composed name).

`apply` renames whole words (runs of [A-Za-z0-9_], the `\\b` of the stack's
sed) in every tracked text file of the tree: the game source (src, include,
asm, data, ldscript.txt, sym_*.txt) and everything under port/ that names game
symbols (generated tables, comments, the Gate 0 scripts, the tests' symbol
lists). It renames each word at most once, so the result does not depend on
the order of the rows. It writes nothing and exits 2 when anything is
ambiguous:
  - a row's new name is already in the tree while its old name still is
    (half applied, or another symbol has that name): `--allow-clash NAME`
    accepts it after a look;
  - an old name is in a protected file (src/eventscr.c stays off-limits for
    renames, naming-proposals/HOLD-BACK.md), which would leave the build with
    two names for one symbol;
  - an old name touches a byte >= 0x80 (the whole-word rule may differ there);
  - an old name is inside a binary file.
`--drop-refused` instead leaves the refused rows out (every row is renamed
everywhere or nowhere, so any subset of the map builds) and renames the rest.
A row whose old name is gone and whose new name is there counts as already
applied, so a second run changes nothing (idempotent). Held-back rows
(link arena, event-script family) are applied: they stay out of upstream PRs
only. `--skip-hold` and `--skip-offlimits` leave those rows out instead.

After writing, `apply` re-keys the argorder accept list (D-52): the sweep keys
each judged call site by a hash of its function name and expression text, so a
rename inside either one leaves the entry stale although the judgment still
holds (port-plan/pending/names11.md). Only entries whose old-name key matches a
flagged site of the renamed tree change, to that site's new key.

`verify-stack` applies the map to every file of the stack's base commit and
checks the result is the stack's tip, byte for byte. `crosscheck` compares the
tree's HEAD after the rename with a three-way merge (git merge-file) of the
stack's change and the port's change, file by file. `check-elf` compares the
symbol tables of a build before and after the rename (the GBA ELF: every
symbol at the same value, size, type and binding; a host binary, whose
addresses may move with the longer or shorter generated stub-name strings:
--names-only).
`lookup` prints the row of a name, old or new (port-plan's documents and old
fault reports keep the old names).

The map and this tool are never renamed (EXCLUDE below).
"""
import argparse
import collections
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

WORD = re.compile(rb"[A-Za-z0-9_]+")
HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_MAP = os.path.join(HERE, "names_map.tsv")
# The naming stack's last rename-only commit (name-final11, wave 7). The two
# commits after it on name-final11 (690b0fa8, 70e09f82) are structural edits,
# not renames, so derive and verify-stack would stop on them: the default tip
# is this commit, not the branch (port-plan/pending/names11.md).
DEFAULT_TIP = "257b1607bdd2d47f16fc9b6fa46b73eb83c34574"
# Never renamed: src/eventscr.c is off-limits (HOLD-BACK.md); an old name
# there refuses the row.
PROTECTED = ("src/eventscr.c",)
# Never touched: the map and this tool (they name the old names on purpose).
EXCLUDE = ("port/tools/names_map.tsv", "port/tools/apply_names.py")
COLUMNS = ("old_name", "new_name", "kind", "gba_addr", "defined_in", "commit", "hold", "offlimits", "touches")
# naming-proposals/HOLD-BACK.md: renamed locally, never in an upstream PR.
HOLD_RULES = (("link-arena", re.compile(r"^src/sio[^/]*\.c$|^src/sio[^/]*\.o$|^include/sio[^/]*\.h$")),
              ("event-script", re.compile(r"^src/eventscr[^/]*\.[co]$")))
# port-plan/HANDOFF.md "Rules in force": areas other contributors work on.
OFFLIMITS = (("prep", re.compile(r"^src/prep[^/]*\.[co]$|^include/prep[^/]*\.h$")),
             ("helpbox", re.compile(r"helpbox[^/]*\.[cho]$")),
             ("opinfo", re.compile(r"^src/opinfo[^/]*\.[co]$|^include/opinfo[^/]*\.h$")),
             ("classchg-sel", re.compile(r"^src/classchg-sel[^/]*\.[co]$")),
             ("eventscr.c", re.compile(r"^src/eventscr\.[co]$")),
             ("banim/ekr", re.compile(r"^src/banim-[^/]*\.[co]$|^include/(ekr|efx|banim)[^/]*\.h$")),
             ("bmmap", re.compile(r"^src/bmmap[^/]*\.[co]$|^include/bmmap[^/]*\.h$")),
             ("map-flood", re.compile(r"^src/(bmidoten|mapflood|map-flood)[^/]*\.[co]$|^include/bmidoten\.h$")))


def die(msg, code=2):
    sys.stderr.write(f"apply_names: {msg}\n")
    sys.exit(code)


def git(repo, *args, data=None):
    r = subprocess.run(["git", "-C", repo] + list(args), input=data, capture_output=True)
    if r.returncode != 0:
        die(f"git {' '.join(args)}: {r.stderr.decode(errors='replace').strip()}")
    return r.stdout


# ---------------------------------------------------------------------------
# The map.

class Row:
    __slots__ = COLUMNS

    def __init__(self, **kw):
        for c in COLUMNS:
            setattr(self, c, kw.get(c, "-") or "-")


def load_map(path):
    rows = []
    header = None
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            cells = line.split("\t")
            if header is None:
                header = cells
                if header[:2] != ["old_name", "new_name"]:
                    die(f"{path}:{n}: the header must start with old_name, new_name")
                continue
            if len(cells) != len(header):
                die(f"{path}:{n}: {len(cells)} cells, the header has {len(header)}")
            rows.append(Row(**dict(zip(header, cells))))
    check_map(rows, path)
    return rows


def check_map(rows, where):
    olds, news = collections.Counter(r.old_name for r in rows), collections.Counter(r.new_name for r in rows)
    bad = [f"old name {o} has {n} rows" for o, n in olds.items() if n > 1]
    bad += [f"new name {o} is given to {n} old names" for o, n in news.items() if n > 1]
    bad += [f"{o} is an old name and a new name (a chain: compose it)" for o in olds if o in news]
    word = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
    bad += [f"{r.old_name} -> {r.new_name}: not identifiers" for r in rows
            if not word.match(r.old_name) or not word.match(r.new_name)]
    bad += [f"{r.old_name}: old and new are the same" for r in rows if r.old_name == r.new_name]
    if bad:
        die(f"{where}: the map is ambiguous:\n  " + "\n  ".join(bad))


def write_map(path, rows, comments):
    with open(path, "w", encoding="utf-8") as f:
        for c in comments:
            f.write(f"# {c}\n" if c else "#\n")
        f.write("\t".join(COLUMNS) + "\n")
        for r in rows:
            f.write("\t".join(getattr(r, c) for c in COLUMNS) + "\n")


def rename_bytes(data, table):
    """Whole-word rename; returns (new data, Counter of old names renamed)."""
    hits = collections.Counter()

    def sub(m):
        w = m.group()
        n = table.get(w)
        if n is None:
            return w
        hits[w] += 1
        return n
    return WORD.sub(sub, data), hits


# ---------------------------------------------------------------------------
# apply / status

def tracked_files(root):
    out = git(root, "ls-files", "-z", "--cached")
    return sorted(set(p.decode() for p in out.split(b"\0") if p))


def scan(root, rows):
    """Where each old and new name appears in the tree (text files only)."""
    old = {r.old_name.encode(): r for r in rows}
    new = {r.new_name.encode(): r for r in rows}
    old_in = collections.defaultdict(collections.Counter)   # old name -> {file: count}
    new_in = collections.defaultdict(collections.Counter)
    binary_hits, nonascii = [], []
    for path in tracked_files(root):
        if path in EXCLUDE:
            continue
        full = os.path.join(root, path)
        if os.path.islink(full) or not os.path.isfile(full):
            continue
        with open(full, "rb") as f:
            data = f.read()
        is_binary = b"\0" in data
        for m in WORD.finditer(data):
            w = m.group()
            if w in old:
                if is_binary:
                    binary_hits.append((path, w.decode()))
                    continue
                old_in[w.decode()][path] += 1
                s, e = m.start(), m.end()
                if (s and data[s - 1] >= 0x80) or (e < len(data) and data[e] >= 0x80):
                    nonascii.append((path, w.decode()))
            elif w in new and not is_binary:
                new_in[w.decode()][path] += 1
    return old_in, new_in, binary_hits, nonascii


def classify(rows, old_in, new_in, binary_hits, nonascii, allow_clash):
    status, why = {}, {}
    bin_names = collections.defaultdict(list)
    for p, w in binary_hits:
        bin_names[w].append(p)
    na_names = collections.defaultdict(list)
    for p, w in nonascii:
        na_names[w].append(p)
    for r in rows:
        o, n = r.old_name, r.new_name
        prot = [p for p in old_in.get(o, {}) if p in PROTECTED]
        if prot:
            status[o], why[o] = "refused", f"old name in protected file {', '.join(prot)}"
        elif o in bin_names:
            status[o], why[o] = "refused", f"old name inside binary file {', '.join(sorted(set(bin_names[o])))}"
        elif o in na_names:
            status[o], why[o] = "refused", f"old name next to a non-ASCII byte in {', '.join(sorted(set(na_names[o])))}"
        elif o in old_in and n in new_in and n not in allow_clash:
            status[o], why[o] = "refused", (f"new name {n} already in {', '.join(sorted(new_in[n]))} "
                                            f"while the old name is still in {len(old_in[o])} files")
        elif o in old_in:
            status[o] = "rename"
        elif n in new_in:
            status[o] = "applied"
        else:
            status[o] = "absent"
    return status, why


def area_of(path):
    if path.startswith("port/"):
        parts = path.split("/")
        return "/".join(parts[:2]) if len(parts) > 2 else path
    top = path.split("/")[0]
    return top if "/" in path else path


def cmd_apply(args, write):
    root = os.path.abspath(args.root)
    rows = load_map(args.map)
    total_rows = len(rows)
    skipped = []
    if args.skip_hold:
        skipped += [r for r in rows if r.hold != "-"]
    if args.skip_offlimits:
        skipped += [r for r in rows if r.offlimits != "-" and r not in skipped]
    rows = [r for r in rows if r not in skipped]
    old_in, new_in, binary_hits, nonascii = scan(root, rows)
    status, why = classify(rows, old_in, new_in, binary_hits, nonascii, set(args.allow_clash or ()))
    refused = [r for r in rows if status[r.old_name] == "refused"]
    st = collections.Counter(status.values())
    print(f"apply_names: map {os.path.relpath(args.map, root) if args.map.startswith(root) else args.map}: "
          f"{total_rows} rows" + (f", {len(skipped)} left out by --skip-hold/--skip-offlimits" if skipped else ""))
    print(f"apply_names: tree {root}: rename {st['rename']}, already applied {st['applied']}, "
          f"absent {st['absent']}, refused {st['refused']}")
    for r in refused:
        print(f"  REFUSED {r.old_name} -> {r.new_name}: {why[r.old_name]}")
    if refused and not args.drop_refused:
        if write:
            print("apply_names: nothing written (refused rows above; --drop-refused renames the rest, "
                  "--allow-clash NAME accepts a clash after a look)")
        return 2
    active = {r.old_name.encode(): r.new_name.encode() for r in rows if status[r.old_name] == "rename"}
    files = sorted(set(p for o in active for p in old_in[o.decode()]))
    per_file = []
    per_name = collections.Counter()
    for path in files:
        full = os.path.join(root, path)
        with open(full, "rb") as f:
            data = f.read()
        out, hits = rename_bytes(data, active)
        if not hits:
            continue
        per_file.append((path, sum(hits.values()), len(hits)))
        per_name.update({k.decode(): v for k, v in hits.items()})
        if write:
            with open(full, "wb") as f:
                f.write(out)
    verb = "renamed" if write else "would rename"
    kinds = {r.old_name: r.kind for r in rows}
    holds = {r.old_name: r.hold for r in rows}
    print(f"apply_names: {verb} {len(per_name)} names, {sum(n for _, n, _ in per_file)} words in {len(per_file)} files")
    by_area = collections.Counter()
    by_area_files = collections.Counter()
    for path, n, _ in per_file:
        by_area[area_of(path)] += n
        by_area_files[area_of(path)] += 1
    print("  by place: " + ", ".join(f"{a} {n} in {by_area_files[a]}" for a, n in sorted(by_area.items())))
    print("  by kind:  " + ", ".join(f"{k} {n}" for k, n in sorted(collections.Counter(kinds[o] for o in per_name).items())))
    print("  held back (applied here, never in an upstream PR): " + (", ".join(
        f"{k} {n}" for k, n in sorted(collections.Counter(holds[o] for o in per_name if holds[o] != "-").items())) or "none"))
    for path, n, k in per_file:
        print(f"  {path}\t{n} words, {k} names")
    if args.report:
        with open(args.report, "w", encoding="utf-8") as f:
            f.write("file\twords\tnames\n")
            for path, n, k in per_file:
                f.write(f"{path}\t{n}\t{k}\n")
            f.write("\nold_name\tnew_name\tstatus\twords\tfiles\twhy\n")
            for r in rows:
                o = r.old_name
                f.write(f"{o}\t{r.new_name}\t{status[o]}\t{per_name.get(o, 0)}\t{len(old_in.get(o, {}))}\t{why.get(o, '')}\n")
    in_effect = [r for r in rows if status[r.old_name] in ("rename", "applied")]
    rekeyed = rekey_argorder(root, in_effect) if write else True
    if refused:
        print(f"apply_names: {len(refused)} refused rows left out (--drop-refused); every other row applied")
    return 0 if rekeyed else 1


# port/tools/argorder_sweep.py (D-52) keys each call site judged by hand in
# port/tools/argorder_accept.txt as file:function:hash, the hash taken over the
# site's kind, function name and expression text (its site_key). A rename in
# the function name or in the expression changes the key, and the entry goes
# stale although the judgment still holds. So after a rename, apply runs the
# sweep on the renamed tree (both views) and, for every flagged site, computes
# the key the site had under the old names (the map inverted: the rename is one
# to one); an accept entry that still has that old key gets the new one.
ARGORDER_SWEEP = "port/tools/argorder_sweep.py"
ARGORDER_ACCEPT = "port/tools/argorder_accept.txt"


def argorder_key(path, kind, func, text):
    """argorder_sweep.py's site_key, for one site."""
    t = re.sub(r"\s+", "", text)
    h = hashlib.sha1(("%s|%s|%s" % (kind, func, t)).encode()).hexdigest()[:8]
    return "%s:%s:%s" % (path, func, h)


def rekey_argorder(root, rows):
    """False when the re-key could not run (the renames are written by then:
    apply warns and exits 1; running apply again re-keys)."""
    sweep, accept = os.path.join(root, ARGORDER_SWEEP), os.path.join(root, ARGORDER_ACCEPT)
    if not (os.path.isfile(sweep) and os.path.isfile(accept)):
        return True

    def fail(msg):
        print(f"apply_names: WARNING: {ARGORDER_ACCEPT} not re-keyed: {msg}. The renames are written; "
              "run apply again once this is fixed (it re-keys and renames nothing else)")
        return False
    inv = {r.new_name.encode(): r.old_name.encode() for r in rows}
    sites = []
    with tempfile.TemporaryDirectory() as d:
        for view in ("gba", "pc"):
            js = os.path.join(d, view + ".json")
            p = subprocess.run([sys.executable, sweep, "--root", root, "--view", view, "-j", "4",
                                "--accept", accept, "--json", js], capture_output=True, text=True)
            if p.returncode != 0 or not os.path.exists(js):
                return fail(f"{ARGORDER_SWEEP} --view {view} failed: {p.stderr.strip()[-400:]}")
            with open(js, encoding="utf-8") as f:
                sites += json.load(f)["sites"]
    old_to_new = {}
    for st in sites:
        if st.get("verdict") not in ("conflict", "unknown"):
            continue
        func = st["func"].split("::")[-1]
        new_key = argorder_key(st["file"], st["kind"], func, st["text"])
        if new_key != st.get("key"):
            return fail(f"{new_key} is not the sweep's key {st.get('key')} "
                        "(argorder_sweep.py's site_key changed: update argorder_key)")
        old_func = rename_bytes(func.encode(), inv)[0].decode()
        old_text = rename_bytes(st["text"].encode(), inv)[0].decode()
        old_hash = argorder_key(st["file"], st["kind"], old_func, old_text).rsplit(":", 1)[1]
        # The list's own function word was renamed with the rest of the tree.
        stale = "%s:%s:%s" % (st["file"], func, old_hash)
        if stale != new_key:
            old_to_new[stale] = new_key
    current = {st.get("key") for st in sites}
    with open(accept, encoding="utf-8") as f:
        lines = f.read().split("\n")
    changed = []
    for i, line in enumerate(lines):
        body = line.split("#", 1)[0].split()
        if not body:
            continue
        k = body[0]
        if k in old_to_new and k not in current:
            lines[i] = line.replace(k, old_to_new[k], 1)
            changed.append((k, old_to_new[k]))
    if changed:
        with open(accept, "w", encoding="utf-8") as f:
            f.write("\n".join(lines))
    print(f"apply_names: {ARGORDER_ACCEPT}: {len(changed)} judged sites re-keyed (their function name or "
          "expression holds a renamed name; the judgment is unchanged)")
    for k, n in changed:
        print(f"  {k} -> {n.rsplit(':', 1)[1]}")
    return True


def cmd_lookup(args):
    """Old docs (port-plan/, DIVERGENCES IDs, fault reports) keep the old
    names: print the row for each name given, old or new."""
    rows = load_map(args.map)
    by = {}
    for r in rows:
        by[r.old_name] = r
        by[r.new_name] = r
    rc = 0
    for n in args.names:
        r = by.get(n)
        if r is None:
            print(f"{n}: not in the map (not renamed by the naming stack)")
            rc = 1
        else:
            print(f"{r.old_name} -> {r.new_name}  ({r.kind}, {r.gba_addr}, {r.defined_in}, {r.commit}"
                  + (f", hold {r.hold}" if r.hold != "-" else "") + ")")
    return rc


def cmd_status(args):
    root = os.path.abspath(args.root)
    rows = load_map(args.map)
    old_in, new_in, binary_hits, nonascii = scan(root, rows)
    status, why = classify(rows, old_in, new_in, binary_hits, nonascii, set())
    st = collections.Counter(status.values())
    print(f"apply_names status: {len(rows)} rows: rename {st['rename']}, already applied {st['applied']}, "
          f"absent {st['absent']}, refused {st['refused']}")
    for r in rows:
        if status[r.old_name] in ("refused",):
            print(f"  REFUSED {r.old_name} -> {r.new_name}: {why[r.old_name]}")
    return 0 if st["rename"] == 0 and st["refused"] == 0 else 1


# ---------------------------------------------------------------------------
# verify-stack / crosscheck

def tree_blobs(repo, rev):
    out = git(repo, "ls-tree", "-r", "-z", "--full-tree", rev)
    res = {}
    for ent in out.split(b"\0"):
        if not ent:
            continue
        meta, path = ent.split(b"\t", 1)
        mode, typ, oid = meta.split()
        if typ == b"blob":
            res[path.decode()] = (mode.decode(), oid.decode())
    return res


def read_blobs(repo, oids):
    """Contents of many blobs with one git cat-file --batch."""
    oids = sorted(set(oids))
    if not oids:
        return {}
    p = subprocess.run(["git", "-C", repo, "cat-file", "--batch"], input=("\n".join(oids) + "\n").encode(),
                       capture_output=True)
    if p.returncode != 0:
        die("git cat-file --batch failed")
    out, pos, res = p.stdout, 0, {}
    for oid in oids:
        nl = out.index(b"\n", pos)
        head = out[pos:nl].split()
        size = int(head[2])
        res[oid] = out[nl + 1:nl + 1 + size]
        pos = nl + 1 + size + 1
    return res


def resolve_base(repo, base, tip, head="HEAD"):
    tip_c = git(repo, "rev-parse", "--verify", tip + "^{commit}").decode().strip()
    if base:
        base_c = git(repo, "rev-parse", "--verify", base + "^{commit}").decode().strip()
    else:
        base_c = git(repo, "merge-base", head, tip_c).decode().strip()
    return base_c, tip_c


def cmd_verify_stack(args):
    repo = os.path.abspath(args.repo)
    rows = load_map(args.map)
    base, tip = resolve_base(repo, args.base, args.tip)
    table = {r.old_name.encode(): r.new_name.encode() for r in rows}
    a, b = tree_blobs(repo, base), tree_blobs(repo, tip)
    if set(a) != set(b):
        die(f"the stack adds or removes files ({len(set(a) ^ set(b))}); verify-stack compares renames only")
    blobs = read_blobs(repo, [v[1] for v in a.values()] + [v[1] for v in b.values()])
    same = changed_ok = nbin = 0
    bad = []
    for path in sorted(a):
        x, y = blobs[a[path][1]], blobs[b[path][1]]
        if b"\0" in x:
            nbin += 1
            if x != y:
                bad.append((path, "binary file changed by the stack"))
            continue
        out, hits = rename_bytes(x, table)
        if path in PROTECTED and hits:
            bad.append((path, "protected file holds old names"))
        if out != y:
            bad.append((path, "differs from the tip after the rename"))
        elif x == y:
            same += 1
        else:
            changed_ok += 1
    print(f"verify-stack: base {base[:8]} tip {tip[:8]}, map {len(rows)} rows: {len(a)} files; "
          f"{changed_ok} renamed files equal the tip byte for byte, {same} text files with no old name, "
          f"{nbin} binary files unchanged, {len(bad)} differ")
    for p, w in bad[:50]:
        print(f"  {p}: {w}")
    return 0 if not bad else 1


def merge_file(ours, base, theirs):
    with tempfile.TemporaryDirectory() as d:
        paths = []
        for name, data in (("ours", ours), ("base", base), ("theirs", theirs)):
            p = os.path.join(d, name)
            with open(p, "wb") as f:
                f.write(data)
            paths.append(p)
        r = subprocess.run(["git", "merge-file", "-p", "-q"] + paths, capture_output=True)
        return r.returncode, r.stdout


def cmd_crosscheck(args):
    """The renamed HEAD of the tree (computed in memory, from git objects)
    against a 3-way merge of the stack's change and the port's change."""
    root = os.path.abspath(args.root)
    rows = load_map(args.map)
    base, tip = resolve_base(root, args.base, args.tip)
    table = {r.old_name.encode(): r.new_name.encode() for r in rows}
    head = git(root, "rev-parse", "HEAD").decode().strip()
    tb, tt, th = tree_blobs(root, base), tree_blobs(root, tip), tree_blobs(root, head)
    stack_files = {p for p in tb if p in tt and tb[p][1] != tt[p][1]}
    port_files = {p for p in th if tb.get(p, (None, None))[1] != th[p][1]}
    need = set()
    for p in stack_files | port_files:
        for t in (tb, tt, th):
            if p in t:
                need.add(t[p][1])
    blobs = read_blobs(root, need)
    counts = collections.Counter()
    problems = []
    for p in sorted(stack_files | port_files):
        if p in EXCLUDE:
            continue
        if p not in th:
            problems.append((p, "the stack renames in a file the port removed"))
            continue
        ours = blobs[th[p][1]]
        if b"\0" in ours:
            continue
        renamed, hits = rename_bytes(ours, table)
        if p in PROTECTED and hits:
            problems.append((p, "protected file holds old names"))
            continue
        if p in stack_files and p not in port_files:
            counts["stack only"] += 1
            if renamed != blobs[tt[p][1]]:
                problems.append((p, "stack-only file: the rename differs from the tip"))
        elif p in stack_files:
            code, merged = merge_file(ours, blobs[tb[p][1]], blobs[tt[p][1]])
            again, more = rename_bytes(merged, table)
            if code == 0 and merged == renamed:
                counts["both, equal to the clean 3-way merge"] += 1
            elif code == 0 and again == renamed:
                # The merge keeps old names in lines only the port wrote
                # (comments, PC_PORT code); the rename renames them too.
                counts["both, equal to the clean 3-way merge once the port's own lines are renamed"] += 1
                print(f"  old names in the port's own lines: {p}: " +
                      ", ".join(f"{k.decode()} x{v}" for k, v in sorted(more.items())))
            elif code == 0:
                counts["both, differs from the clean 3-way merge"] += 1
                problems.append((p, "differs from the clean 3-way merge"))
            else:
                counts["both, 3-way merge conflicts (check by hand)"] += 1
                problems.append((p, f"3-way merge has {code} conflicts: check the rename by hand"))
        else:
            if hits:
                counts["port only, renamed"] += 1
                print(f"  port-only use of old names: {p}: " +
                      ", ".join(f"{k.decode()} x{v}" for k, v in sorted(hits.items())))
            else:
                counts["port only, no old names"] += 1
    print(f"crosscheck: base {base[:8]} tip {tip[:8]} head {head[:8]}: " +
          "; ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    for p, w in problems:
        print(f"  {p}: {w}")
    hard = [x for x in problems if "conflicts" not in x[1]]
    return 0 if not hard else 1


# Symbol names a build makes from a game symbol's name: gen_host_link.py's
# generated stubs keep a call counter sCount_<function> (their log line
# "[stub] <function>" prints the new name too).
DERIVED = ("sCount_",)


def elf_table(elf):
    """Symbols of the loaded image: ABS, common, undefined, or in a section
    with the A flag (symbols in debug sections move with the names' lengths)."""
    sec = subprocess.run(["readelf", "-SW", elf], capture_output=True, text=True, check=True).stdout
    alloc = set()
    for line in sec.splitlines():
        m = re.match(r"^\s*\[\s*(\d+)\]\s+(\S*)\s+(\S+)\s+[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s*(\S*)", line)
        if m and "A" in m.group(4):
            alloc.add(m.group(1))
    out = subprocess.run(["readelf", "-sW", elf], capture_output=True, text=True, check=True).stdout
    syms = []
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 8 and p[0][:-1].isdigit() and p[0].endswith(":") and p[3] not in ("SECTION", "FILE") \
                and (p[6] in ("ABS", "COM", "UND") or p[6] in alloc):
            syms.append((p[7], int(p[1], 16), p[2], p[3], p[4]))
    return syms


def cmd_check_elf(args):
    """The renamed build's symbols are the old build's, renamed: same values,
    sizes, types and bindings (or, with --names-only, the same names)."""
    rows = load_map(args.map)
    table = {r.old_name: r.new_name for r in rows}
    for path in (args.old, args.new):
        with open(path, "rb") as f:
            if f.read(4) != b"\x7fELF":
                die(f"{path} is not an ELF file (a PE .exe: compare its sections with objdump instead)")
    old, new = elf_table(args.old), elf_table(args.new)

    def key(s, rename):
        name = table.get(s[0], s[0]) if rename else s[0]
        if rename:
            # GCC's function-static suffix (name.N) follows its name.
            m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)(\.\d+)$", name)
            if m and m.group(1) in table:
                name = table[m.group(1)] + m.group(2)
            # Names made from a game symbol's name at build time (DERIVED).
            for pre in DERIVED:
                if name.startswith(pre) and name[len(pre):] in table:
                    name = pre + table[name[len(pre):]]
        return (name, s[3]) if args.names_only else (name, s[1], s[2], s[3], s[4])
    a = collections.Counter(key(s, True) for s in old)
    b = collections.Counter(key(s, False) for s in new)
    lost, extra = a - b, b - a
    renamed = sum(1 for s in old if s[0] in table)
    still_old = sorted({s[0] for s in new if s[0] in table})
    print(f"check-elf: {args.old}: {len(old)} symbols, {renamed} of them renamed by the map; "
          f"{args.new}: {len(new)} symbols; "
          f"{sum(lost.values())} expected and missing, {sum(extra.values())} unexpected, "
          f"{len(still_old)} old names left" + (" (names and types only)" if args.names_only else ""))
    for k in list(lost)[:20]:
        print(f"  missing: {k}")
    for k in list(extra)[:20]:
        print(f"  unexpected: {k}")
    for n in still_old[:20]:
        print(f"  old name left: {n}")
    return 0 if not lost and not extra and not still_old else 1


# ---------------------------------------------------------------------------
# derive

def word_pairs(x, y, path):
    wx, wy = WORD.findall(x), WORD.findall(y)
    if len(wx) != len(wy) or WORD.split(x) != WORD.split(y):
        die(f"{path}: the stack changes more than names here (derive takes whole-word renames only)")
    return [(a.decode(), b.decode()) for a, b in zip(wx, wy) if a != b]


def stack_pairs(repo, a, b):
    ta, tb = tree_blobs(repo, a), tree_blobs(repo, b)
    if set(ta) != set(tb):
        die(f"{a[:8]}..{b[:8]} adds, removes or renames files: derive takes whole-word renames only")
    paths = [p for p in ta if ta[p][1] != tb[p][1]]
    blobs = read_blobs(repo, [ta[p][1] for p in paths] + [tb[p][1] for p in paths])
    pairs = collections.defaultdict(set)
    touches = collections.defaultdict(set)
    for p in paths:
        for o, n in word_pairs(blobs[ta[p][1]], blobs[tb[p][1]], p):
            pairs[o].add(n)
            touches[o].add(p)
    bad = [f"{o} -> {', '.join(sorted(n))}" for o, n in pairs.items() if len(n) > 1]
    if bad:
        die(f"{a[:8]}..{b[:8]}: an old name with two new names:\n  " + "\n  ".join(bad))
    return {o: n.pop() for o, n in pairs.items()}, touches


def elf_symbols(elf):
    out = subprocess.run(["readelf", "-sW", elf], capture_output=True, text=True, check=True).stdout
    res = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 8 and p[0].endswith(":") and p[3] in ("FUNC", "OBJECT", "NOTYPE"):
            res.setdefault(p[7], (int(p[1], 16), p[3]))
    return res


def map_objects(mapfile):
    res, cur = {}, None
    obj = re.compile(r"^ (?:\S+)?\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s+(\S+\.o)\b")
    sym = re.compile(r"^\s+0x[0-9a-f]+\s+([A-Za-z_][A-Za-z0-9_]*)\s*$")
    # RAM symbols placed by ldscript.txt / sym_*.txt: "0x... name = ."
    assign = re.compile(r"^\s+0x[0-9a-f]+\s+([A-Za-z_][A-Za-z0-9_]*) = \.")
    with open(mapfile, errors="replace") as f:
        for line in f:
            m = obj.match(line)
            if m:
                cur = m.group(1)
                continue
            m = sym.match(line)
            if m:
                res.setdefault(m.group(1), cur)
                continue
            m = assign.match(line)
            if m:
                res.setdefault(m.group(1), "(linker script)")
    return res


def cmd_derive(args):
    repo = os.path.abspath(args.repo)
    base, tip = resolve_base(repo, args.base, args.tip)
    merges = git(repo, "rev-list", "--merges", f"{base}..{tip}").split()
    if merges:
        die(f"{base[:8]}..{tip[:8]} has merge commits: derive reads a straight stack")
    commits = git(repo, "rev-list", "--reverse", f"{base}..{tip}").decode().split()
    net, touches = stack_pairs(repo, base, tip)
    # Who renamed it first, and the names in between (the stack's commits compose).
    first, via, cur = {}, collections.defaultdict(list), {}
    for c in commits:
        p, _ = stack_pairs(repo, c + "^", c)
        back = {v: k for k, v in cur.items()}
        for o, n in p.items():
            orig = back.get(o, o)
            if orig != o:
                via[orig].append(o)
            first.setdefault(orig, c[:8])
            cur[orig] = n
    composed = {o: n for o, n in cur.items() if o != n}
    if composed != net:
        die("the stack's commits, composed, do not give its net change: "
            f"{sorted(set(composed.items()) ^ set(net.items()))[:10]}")
    syms = elf_symbols(args.elf) if args.elf else {}
    objs = map_objects(args.mapfile) if args.mapfile else {}
    rows = []
    kind_name = {"FUNC": "function", "OBJECT": "data", "NOTYPE": "label"}
    for o in sorted(net, key=lambda s: s.lower()):
        sym = syms.get(o)
        defined = objs.get(o) or "-"
        files = sorted(touches[o])
        hold = "-"
        for name, rule in HOLD_RULES:
            if rule.search(defined) or any(rule.search(f) for f in files):
                hold = name
                break
        off = sorted({name for name, rule in OFFLIMITS if rule.search(defined)})
        touched_off = sorted({name for name, rule in OFFLIMITS if any(rule.search(f) for f in files)} - set(off))
        notes = []
        if via[o]:
            notes.append("via " + ",".join(via[o]))
        rows.append(Row(old_name=o, new_name=net[o],
                        kind=kind_name.get(sym[1], "?") if sym else ("?" if syms else "-"),
                        gba_addr=f"0x{sym[0]:08X}" if sym else "-",
                        defined_in=defined, commit=first[o] + (f" ({' '.join(notes)})" if notes else ""),
                        hold=hold, offlimits=",".join(off) or "-",
                        touches=",".join(files) + (f" [also off-limits: {','.join(touched_off)}]" if touched_off else "")))
    check_map(rows, "derived map")
    subj = {c[:8]: git(repo, "log", "-1", "--format=%s", c).decode().strip() for c in commits}
    comments = [
        "names_map.tsv: the naming stack's renames for the port (port/tools/apply_names.py apply).",
        f"Made by: python3 port/tools/apply_names.py derive --base {base[:8]} --tip {args.tip}"
        + (" --elf/--mapfile (old-name GBA build)" if syms else ""),
        f"Stack: {base[:8]}..{tip[:8]} ({len(commits)} commits, whole-word renames only), "
        f"{len(rows)} rows (the commits composed: a name renamed twice keeps its last name).",
        "Columns: old_name, new_name; kind (GBA ELF symbol type: function, data, label = NOTYPE); gba_addr;",
        "defined_in (object in the GBA link map, or (linker script): RAM placed by ldscript.txt/sym_*.txt);",
        "commit (first stack commit that renamed it; via: the names it had in between);",
        "hold (naming-proposals/HOLD-BACK.md: applied locally, never in an upstream PR): link-arena when the",
        "rename touches src/sio*.c or include/sio*.h (defined there or used there: a rename goes everywhere at",
        "once, so a PR with it would touch those files), event-script when it touches src/eventscr*.c;",
        "offlimits (the defining file is in an area HANDOFF.md lists as off-limits for new names; the stack",
        "renamed it anyway, so it is applied here too); touches (files the stack changed for it).",
        "",
    ] + [f"{c} {s}" for c, s in subj.items()]
    write_map(args.output, rows, comments)
    print(f"derive: {len(rows)} rows from {len(commits)} commits ({base[:8]}..{tip[:8]}) -> {args.output}")
    print("  kinds: " + ", ".join(f"{k} {v}" for k, v in sorted(collections.Counter(r.kind for r in rows).items())))
    print("  hold:  " + ", ".join(f"{k} {v}" for k, v in sorted(collections.Counter(r.hold for r in rows).items())))
    print("  off-limits definitions: " + ", ".join(
        f"{k} {v}" for k, v in sorted(collections.Counter(r.offlimits for r in rows).items())))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("apply", "status", "verify-stack", "crosscheck", "check-elf", "lookup", "derive"):
        p = sub.add_parser(name)
        if name == "lookup":
            p.add_argument("names", nargs="+")
        if name == "check-elf":
            p.add_argument("--old", required=True, help="the ELF built before the rename")
            p.add_argument("--new", required=True, help="the ELF built after it")
            p.add_argument("--names-only", action="store_true",
                           help="compare names and types only (a host binary, whose addresses may move)")
        if name in ("apply", "status", "crosscheck"):
            p.add_argument("--root", default=".")
        if name in ("verify-stack", "derive"):
            p.add_argument("--repo", default=".")
        if name != "derive":
            p.add_argument("--map", default=DEFAULT_MAP)
        if name in ("verify-stack", "crosscheck", "derive"):
            p.add_argument("--base", default=None, help="default: merge-base of HEAD and --tip")
            p.add_argument("--tip", default=DEFAULT_TIP)
        if name == "apply":
            p.add_argument("--dry-run", action="store_true")
            p.add_argument("--drop-refused", action="store_true")
            p.add_argument("--skip-hold", action="store_true")
            p.add_argument("--skip-offlimits", action="store_true")
            p.add_argument("--allow-clash", action="append", metavar="NAME")
            p.add_argument("--report", metavar="FILE")
        if name == "derive":
            p.add_argument("--elf")
            p.add_argument("--mapfile")
            p.add_argument("-o", "--output", default=DEFAULT_MAP)
    args = ap.parse_args()
    if args.cmd == "apply":
        return cmd_apply(args, write=not args.dry_run)
    if args.cmd == "status":
        return cmd_status(args)
    if args.cmd == "verify-stack":
        return cmd_verify_stack(args)
    if args.cmd == "crosscheck":
        return cmd_crosscheck(args)
    if args.cmd == "check-elf":
        return cmd_check_elf(args)
    if args.cmd == "lookup":
        return cmd_lookup(args)
    return cmd_derive(args)


if __name__ == "__main__":
    sys.exit(main())
