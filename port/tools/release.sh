#!/bin/bash
# One-command release: commit, tag, package, install into the play folder, back up.
#
#   port/tools/release.sh VERSION MESSAGE_FILE [--no-install]
#
# Before: the change is in the working tree, docs/port/CHANGELOG.md has "## VERSION (unreleased)" at the
# top, and port/tools/quickcheck.sh passed on exactly this tree (build/quickcheck.stamp).
# MESSAGE_FILE: the commit message; its first line "VERSION: title" also names the tag.
#
# Steps: date the CHANGELOG heading; commit (docs, port, src, include) and tag; move the other main lines
# (port-d1a-spike, port-reforged and the WORKTREES' branches) to the commit; package the Windows zip
# (port/tools/package.sh) into WIN_ROOT/releases; copy the changed files into WIN_ROOT/source; keep this
# build's binaries as the next quickcheck's baseline (~/fe8-base/VERSION/bin); push the main lines and
# the tag to the backup remote "local"; install into WIN_ROOT/play (the old exe kept in play/backups),
# refusing while the game runs, and check that no save and not the settings file changed.
# Publishing to GitHub is separate (port/tools/publish.ps1, on Windows) and needs the owner's go-ahead.
#
# Local paths come from ~/.config/fe8pc-release.conf (never in git): WIN_ROOT, WORKTREES, SETTINGS_FILE.
set -euo pipefail
V=${1:?usage: release.sh VERSION MESSAGE_FILE [--no-install]}
MSG=${2:?message file}
INSTALL=1; [ "${3:-}" = --no-install ] && INSTALL=0
cd "$(git rev-parse --show-toplevel)"
REPO=$PWD
CONF=${FE8PC_RELEASE_CONF:-$HOME/.config/fe8pc-release.conf}
. "$CONF"
: "${WIN_ROOT:?}"
step() { echo "[$(date +%H:%M:%S)] $*"; }
die() { echo "RELEASE STOPPED: $*"; exit 1; }

[[ $V =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "version must look like v1.2.3"
git rev-parse -q --verify "refs/tags/$V" > /dev/null && die "tag $V exists"
[ -f "$MSG" ] || die "no message file $MSG"
head -1 "$MSG" | grep -q "^$V: " || die "the message's first line must start with '$V: '"
grep -q "^## $V (unreleased)" docs/port/CHANGELOG.md || die "CHANGELOG has no '## $V (unreleased)' heading"
PREV=$(git describe --tags --abbrev=0 --match 'v[0-9]*' HEAD)

# the quick check must have passed on this very tree
STAMP=$(cat build/quickcheck.stamp 2>/dev/null || true)
NOW="$(git rev-parse HEAD) $(git diff HEAD -- . ':!build' | sha1sum | cut -c1-40)"
[ "${STAMP% * *}" = "$NOW" ] || die "run port/tools/quickcheck.sh first (no passing check for this tree)"

step "release $V (previous $PREV)"
sed -i "s/^## $V (unreleased)/## $V ($(date +%Y-%m-%d))/" docs/port/CHANGELOG.md
for f in $(git diff --name-only HEAD; git ls-files --others --exclude-standard -- docs port src include); do
  [ -f "$f" ] && sed -i 's/\r$//' "$f"
done
git add -A -- docs port src include README.md
git status --short --untracked-files=no
git commit -q -F <(sed 's/\r$//' "$MSG")
git tag -a "$V" -m "FE8 PC port $(head -1 "$MSG" | sed 's/\r$//')" HEAD
C=$(git rev-parse --short HEAD)
step "committed $C, tagged $V"

# the other main lines follow
HERE=$(git rev-parse --abbrev-ref HEAD)
for w in ${WORKTREES:-}; do [ -d "$HOME/$w" ] && git -C "$HOME/$w" merge --ff-only -q "$HERE"; done
for b in port-d1a-spike port-reforged port-rfo-random; do
  [ "$(git rev-parse "$b")" = "$(git rev-parse HEAD)" ] || git branch -f "$b" HEAD 2>/dev/null || true
done
git branch -f main HEAD 2>/dev/null || true

# package (the build quickcheck made from this tree: only the CHANGELOG's date changed since)
step "package"
bash port/tools/package.sh "$V" | tail -2
ZIP="$WIN_ROOT/releases/FE8-PC-Port-$V-Windows.zip"

# the Windows source copy
step "source copy"
W="$WIN_ROOT/source"; n=0
for f in $(git diff --name-only "$PREV" HEAD); do
  if [ -f "$f" ]; then mkdir -p "$(dirname "$W/$f")"; cp "$f" "$W/$f"; else rm -f "$W/$f"; fi; n=$((n+1))
done
echo "  $n files"

# the next quickcheck's baseline
mkdir -p "$HOME/fe8-base/$V/bin"; cp build/pc-sdl/fe8pc-sdl build/pc-full/fe8pc "$HOME/fe8-base/$V/bin/"

# the backup
step "backup"
git push -q local port-d1a-spike port-reforged port-rfo-random HEAD:refs/heads/main "$V"
echo "  backup main: $(git -C "$(git remote get-url local)" rev-parse --short main)"

# install
if [ $INSTALL = 1 ]; then
  step "install"
  P="$WIN_ROOT/play"
  tasklist.exe 2>/dev/null | grep -i '^fe8pc-sdl.exe' > /dev/null && die "the game is running: close it, then rerun the install part (release committed and packaged)"
  sums() { ( cd "$P" && find . -maxdepth 2 \( -name '*.sav' -o -name '*.sav.*' \) -type f -print0 | sort -z | xargs -0 -r sha1sum ) ; [ -n "${SETTINGS_FILE:-}" ] && [ -f "$SETTINGS_FILE" ] && sha1sum "$SETTINGS_FILE"; true; }
  BEFORE=$(sums)
  mkdir -p "$P/backups"
  [ -f "$P/backups/fe8pc-sdl-$PREV.exe" ] || cp "$P/fe8pc-sdl.exe" "$P/backups/fe8pc-sdl-$PREV.exe"
  T=$(mktemp -d); python3 -I -c 'import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$ZIP" "$T"
  cp "$T/FE8-PC-Port-$V-Windows/fe8pc-sdl.exe" "$T/FE8-PC-Port-$V-Windows/SDL3.dll" "$P/"
  a=$(sha256sum < build/win32-sdl/fe8pc-sdl.exe); b=$(sha256sum < "$P/fe8pc-sdl.exe"); rm -rf "$T"
  [ "$a" = "$b" ] || die "installed exe differs from the build"
  AFTER=$(sums)
  [ "$BEFORE" = "$AFTER" ] || die "a save or the settings file changed during the install"
  echo "  installed exe ${b:0:16}...; saves and settings unchanged ($(echo "$AFTER" | grep -c .) files checked)"
fi
echo "zip sha256 $(sha256sum < "$ZIP" | cut -c1-16)..."
echo "RELEASE $V: DONE (commit $C)"
