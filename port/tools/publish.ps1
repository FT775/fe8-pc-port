# Publish a release made by release.sh to GitHub, from the backup repository (Windows: the GitHub login is
# here). Only with the owner's go-ahead for that version.
#
#   publish.ps1 -Version v1.2.3 [-Private] [-Public] [-NotesFile notes.md] [-DryRun]
#
#   -Private  push main, port-reforged, port-rfo-random and the tag to <owner>/fe8-pc-port-dev
#   -Public   one snapshot commit of the tag's tree on top of the public main (author: the GitHub
#             account's noreply address), pushed to <owner>/fe8-pc-port, and a release with the zip.
#             Refused when the public main moved, or when the scan finds a listed pattern or a
#             ROM / save / state file in the files changed since the last public commit.
#   -NotesFile  release notes; default: release-notes.md (beside this script) with {VERSION} and
#             {WHATSNEW} filled in: the version's CHANGELOG "### In short" part (a few lines for players),
#             or without one the whole section but its "### Technical" part.
#   -DryRun   everything but the pushes and the release.
#
# Local paths (never in git): -Root, the play-copy folder holding backup\fe8.git and releases\
# (default: three folders above this script, as in <root>\source\port\tools). The personal-info scan
# patterns are one regex per line in <root>\backup\publish-scan.txt.
param(
    [Parameter(Mandatory)][string]$Version,
    [switch]$Private, [switch]$Public, [switch]$DryRun,
    [string]$NotesFile = "",
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
)
$ErrorActionPreference = "Stop"
if (-not ($Private -or $Public)) { throw "say -Private and/or -Public" }
$B = Join-Path $Root "backup\fe8.git"
function G { & git -c safe.directory=* -c credential.helper= -c "credential.helper=!gh auth git-credential" -C $B @args; if ($LASTEXITCODE) { throw "git $args failed" } }
$Owner = (gh api user --jq .login); $Id = (gh api user --jq .id)
$Tag = (G rev-parse "$Version^{commit}")
Write-Output "publishing $Version ($($Tag.Substring(0,8))) as $Owner"

if ($Private) {
    $url = "https://github.com/$Owner/fe8-pc-port-dev.git"
    if ($DryRun) { Write-Output "dry run: would push main port-reforged port-rfo-random $Version to $url" }
    else { G push $url main port-reforged port-rfo-random $Version; Write-Output "private: pushed" }
}

if ($Public) {
    $url = "https://github.com/$Owner/fe8-pc-port.git"
    G fetch -q $url "+main:refs/remotes/public/main"
    $P = (G rev-parse refs/remotes/public/main)
    $staged = (& git -c safe.directory=* -C $B rev-parse -q --verify refs/heads/public-main)
    if ($staged -and $staged -ne $P) { throw "public main moved ($P) since the last publish ($staged)" }
    # scan the files changed since the last public commit
    $scan = Join-Path $Root "backup\publish-scan.txt"
    if (-not (Test-Path $scan)) { throw "no scan pattern file $scan" }
    $pat = ((Get-Content $scan | Where-Object { $_ -and -not $_.StartsWith("#") }) -join "|")
    $files = @(G diff --name-only $P $Version)
    $hits = @()
    foreach ($chunk in ($files | ForEach-Object -Begin { $a = @() } -Process { $a += $_; if ($a.Count -ge 200) { , $a; $a = @() } } -End { if ($a.Count) { , $a } })) {
        $hits += @(& git -c safe.directory=* -C $B grep -IlE $pat $Version -- @chunk 2>$null)
    }
    $roms = @(G ls-tree -r --name-only $Version | Where-Object { $_ -match '\.(gba|sav|state)$' })
    Write-Output "scan: $($files.Count) changed files, $($hits.Count) with a listed pattern, $($roms.Count) ROM/save/state files"
    if ($hits.Count -or $roms.Count) { $hits + $roms | Select-Object -First 10; throw "scan refused the publish" }
    # the snapshot commit
    $env:GIT_AUTHOR_NAME = $Owner; $env:GIT_COMMITTER_NAME = $Owner
    $env:GIT_AUTHOR_EMAIL = "$Id+$Owner@users.noreply.github.com"; $env:GIT_COMMITTER_EMAIL = $env:GIT_AUTHOR_EMAIL
    $subject = ((G tag -l --format="%(contents:subject)" $Version) -replace '^FE8 PC port ', '')
    $C = ("FE8 PC port $subject`n`nSee docs/port/CHANGELOG.md.`n" | & git -c safe.directory=* -C $B commit-tree "$Version^{tree}" -p $P)
    if ($LASTEXITCODE) { throw "commit-tree failed" }
    Write-Output "public commit $C"
    # notes
    if (-not $NotesFile) {
        $NotesFile = Join-Path $env:TEMP "fe8pc-notes-$Version.md"
        $head = (Get-Content (Join-Path $PSScriptRoot "release-notes.md") -Raw).Replace("{VERSION}", $Version)
        $log = (G show "${Version}:docs/port/CHANGELOG.md") -join "`n"
        $m = [regex]::Match($log, "(?ms)^## $([regex]::Escape($Version)) \([^\n]*\n(.*?)(?=^## v|\z)")
        if (-not $m.Success) { throw "no CHANGELOG section for $Version" }
        $sec = $m.Groups[1].Value
        $short = [regex]::Match($sec, "(?ms)^### In short[^\n]*\n(.*?)(?=^### |\z)")
        if ($short.Success) { $body = $short.Groups[1].Value.Trim() }
        else { $body = [regex]::Replace($sec, "(?ms)^### Technical.*?(?=^### |\z)", "").Trim() }
        $body = $body -replace '(?m)^### ', '#### '
        Set-Content -Path $NotesFile -Value $head.Replace("{WHATSNEW}", $body) -Encoding utf8
    }
    $zipSrc = Join-Path $Root "releases\FE8-PC-Port-$Version-Windows.zip"
    $zip = Join-Path $env:TEMP "fe8-pc-port-$Version-windows.zip"
    Copy-Item $zipSrc $zip -Force
    if ($DryRun) { Write-Output "dry run: notes in $NotesFile; would push $C to $url main and create release $Version"; Get-Content $NotesFile | Select-Object -First 40; return }
    G update-ref refs/heads/public-main $C
    G push $url "${C}:refs/heads/main"
    gh release create $Version -R "$Owner/fe8-pc-port" --target $C --title "FE8 PC Port $Version" --notes-file $NotesFile --latest $zip
    if ($LASTEXITCODE) { throw "gh release create failed" }
    $asset = (gh release view $Version -R "$Owner/fe8-pc-port" --json assets --jq '.assets[0].size')
    if ([int64]$asset -ne (Get-Item $zipSrc).Length) { throw "uploaded zip size $asset differs" }
    Write-Output "public: pushed, release $Version up ($asset bytes)"
}
