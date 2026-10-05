#!/usr/bin/env bash
# Publish a Status-ESP release (or pre-release) on GitHub.
#
# Usage: bash tools/release.sh [--dry-run]
#
# The version comes from the VERSION file ("0.3.0-rc.1" -> tag v0.3.0-rc.1, published
# as a GitHub pre-release; "0.3.0" -> tag v0.3.0, published as the latest release).
#
# The script REFUSES to do anything (and lists every reason it found) unless:
#   - the working tree is clean (untracked files that are not git-ignored count);
#   - VERSION is valid (MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-rc.N, no leading "v");
#   - the tag v<version> exists neither locally nor on origin;
#   - origin is the Status-ESP repository, and HEAD equals its upstream branch
#     (everything is pushed);
#   - CHANGELOG.md has a "## [<version>]" section with some content;
#   - for a stable version, the current branch is main;
#   - the GitHub CLI (gh) is installed and authenticated; PlatformIO is set up.
#
# Then it:
#   1. builds the firmware (tools/build.sh);
#   2. validates it and prepares dist/v<version>/ (tools/make_release.py):
#      the .bin, version.json and checksums.txt;
#   3. writes the release notes: the CHANGELOG section + an install/verify blurb
#      (+ a "not yet tested on a device" warning for an rc);
#   4. creates the annotated tag v<version>, pushes it to origin, and runs
#      "gh release create" with the three files attached.
#
# --dry-run does steps 1-3 for real (the build is real; nothing leaves this machine
# except read-only checks against origin and GitHub), prints the notes and the exact
# tag/push/gh commands it would run, and creates, pushes and publishes nothing.
#
# Releasing is a public action: an rc is published automatically at the end of a
# batch of work; promoting to a stable release needs the owner's go-ahead.
set -euo pipefail
cd "$(dirname "$0")/.."

REPO="Adam4125-officiel/Status-ESP"
VERSION_RE='^[0-9]+\.[0-9]+\.[0-9]+(-rc\.[0-9]+)?$'

usage() {
    # Print the header comment above (everything up to the first non-comment line).
    awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"
}

dry_run=0
for arg in "$@"; do
    case "$arg" in
        --dry-run) dry_run=1 ;;
        -h | --help) usage; exit 0 ;;
        *) echo "release.sh: unknown argument: $arg" >&2; echo >&2; usage >&2; exit 2 ;;
    esac
done

die() { echo "release.sh: $*" >&2; exit 1; }

command -v git >/dev/null 2>&1 || die "git is not installed"

# --- Preconditions -----------------------------------------------------------
# Every check records its problem instead of stopping at the first one, so a single
# run lists everything that has to be fixed.
problems=()
problem() { problems+=("$*"); }

# Tools
command -v python3 >/dev/null 2>&1 || problem "python3 is not installed"
[ -x .venv/bin/pio ] || problem "PlatformIO is not set up (.venv/bin/pio is missing): run 'bash tools/setup.sh' first"

# Clean tree
dirty="$(git status --porcelain --untracked-files=normal)"
if [ -n "$dirty" ]; then
    dirty_lines="$(printf '%s\n' "$dirty" | wc -l)"
    dirty_more=""
    [ "$dirty_lines" -le 15 ] || dirty_more="
        ... and $((dirty_lines - 15)) more"
    problem "the working tree is not clean (commit, stash or remove these first; untracked files that are not git-ignored count too):
$(printf '%s\n' "$dirty" | head -n 15 | sed 's/^/        /')$dirty_more"
fi

# VERSION
version=""
if [ ! -f VERSION ]; then
    problem "the VERSION file is missing"
else
    read -r version < VERSION || true
    if [[ ! $version =~ $VERSION_RE ]]; then
        problem "VERSION is invalid: '$version' (expected MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-rc.N, no leading 'v' and no spaces, e.g. 0.3.0 or 0.3.0-rc.1)"
        version=""
    fi
fi

# origin must be the Status-ESP repository (the tag is pushed to origin)
origin_url="$(git remote get-url origin 2>/dev/null || true)"
repo_lc="${REPO,,}"
case "${origin_url,,}" in
    *github.com[:/]"$repo_lc" | *github.com[:/]"$repo_lc".git) ;;
    *) problem "the 'origin' remote is not $REPO (it is '${origin_url:-not set}')" ;;
esac

# Tag must not exist yet, locally or on origin
if [ -n "$version" ]; then
    if git rev-parse -q --verify "refs/tags/v$version" >/dev/null; then
        problem "the tag v$version already exists locally (bump VERSION, or delete the tag if it was never published: git tag -d v$version)"
    fi
    rc=0
    git ls-remote --exit-code --tags origin "refs/tags/v$version" >/dev/null 2>&1 || rc=$?
    case "$rc" in
        0) problem "the tag v$version already exists on origin (a published version is never re-released: bump VERSION)" ;;
        2) ;; # 2 = no such ref on the remote: what we want
        *) problem "could not ask origin whether the tag v$version exists (git ls-remote exit code $rc): network or credentials?" ;;
    esac
fi

# Branch, and HEAD pushed (equal to its upstream)
branch="$(git symbolic-ref -q --short HEAD || true)"
if [ -z "$branch" ]; then
    problem "HEAD is detached: check out the release branch first"
else
    upstream="$(git rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null || true)"
    if [ -z "$upstream" ]; then
        problem "the branch '$branch' has no upstream: push it first (git push -u origin $branch)"
    elif ! git fetch --quiet origin 2>/dev/null; then
        problem "could not fetch from origin to check that HEAD is pushed: network or credentials?"
    elif [ "$(git rev-parse HEAD)" != "$(git rev-parse '@{u}')" ]; then
        read -r ahead behind < <(git rev-list --left-right --count 'HEAD...@{u}')
        problem "HEAD is not pushed: '$branch' is $ahead commit(s) ahead of and $behind behind $upstream (push, or pull, first)"
    fi
fi

# A stable release is cut from main
if [ -n "$version" ] && [[ $version != *-rc.* ]] && [ "$branch" != "main" ]; then
    problem "v$version is a stable release, which is only cut from 'main' (current branch: '${branch:-detached}'): merge the work into main first"
fi

# CHANGELOG section for this version (everything up to the next "## [" heading)
changelog_section() {
    awk -v head="## [$1]" '
        index($0, head) == 1 { found = 1; next }
        found && /^## \[/ { exit }
        found && /^\[[^]]+\]: / { exit }   # link reference definitions at the bottom
        found { lines[++n] = $0 }
        END {
            s = 1; while (s <= n && lines[s] ~ /^[[:space:]]*$/) s++
            e = n; while (e >= s && lines[e] ~ /^[[:space:]]*$/) e--
            for (i = s; i <= e; i++) print lines[i]
        }' CHANGELOG.md
}
notes_body=""
if [ -n "$version" ]; then
    if [ ! -f CHANGELOG.md ]; then
        problem "CHANGELOG.md is missing"
    elif ! grep -Eq "^## \[${version//./\\.}\]" CHANGELOG.md; then
        problem "CHANGELOG.md has no '## [$version]' section: write it (it becomes the release notes)"
    else
        notes_body="$(changelog_section "$version")"
        [ -n "$notes_body" ] || problem "the '## [$version]' section of CHANGELOG.md is empty: it becomes the release notes"
    fi
fi

# GitHub CLI
if command -v gh >/dev/null 2>&1; then
    gh auth status --hostname github.com >/dev/null 2>&1 || problem "the GitHub CLI is not authenticated: run 'gh auth login'"
else
    problem "the GitHub CLI (gh) is not installed"
fi

if [ "${#problems[@]}" -gt 0 ]; then
    echo "release.sh: refusing to release${version:+ v$version}:" >&2
    for p in "${problems[@]}"; do
        printf '  - %s\n' "$p" >&2
    done
    exit 1
fi

tag="v$version"
if [[ $version == *-rc.* ]]; then is_rc=1; else is_rc=0; fi
bin_name="Status-ESP-$version.bin"
assets=("dist/$tag/$bin_name" "dist/$tag/version.json" "dist/$tag/checksums.txt")

# --- Build and assets --------------------------------------------------------
echo "==> Building $tag"
bash tools/build.sh

echo "==> Validating the image and preparing dist/$tag/"
python3 tools/make_release.py --tag "$tag"

for f in "${assets[@]}"; do
    [ -f "$f" ] || die "expected release asset is missing: $f"
done
[ -z "$(git status --porcelain --untracked-files=normal)" ] || die "the build modified the working tree; refusing to tag it"

# --- Release notes -----------------------------------------------------------
md5="$(awk '$1 == "MD5" { print $NF }' "dist/$tag/checksums.txt")"
sha256="$(awk '$1 == "SHA256" { print $NF }' "dist/$tag/checksums.txt")"

notes="$(mktemp "${TMPDIR:-/tmp}/status-esp-notes.XXXXXX")"
trap 'rm -f "$notes"' EXIT

blurb="$(cat <<'EOF'
## Install

For the **GeekMagic SmallTV-Ultra** (ESP8266) only.

1. Open `http://<device-ip>/update` in a browser (the IP is shown on the device's screen after boot).
2. Choose `@BIN@` and submit. Do not power the device off during the update.
3. The device restarts and shows the address of its web interface.

To go back to the stock firmware, upload GeekMagic's own `.bin` on the same `/update` page:
the stock images and settings are kept.

## Verify the download

- SHA-256: `@SHA256@`
- MD5: `@MD5@`

Linux: put `checksums.txt` next to the `.bin` and run `cksum -c checksums.txt` (GNU coreutils 9+),
or compare `sha256sum @BIN@` with the value above.
Windows (PowerShell): `Get-FileHash @BIN@ -Algorithm SHA256`.

`version.json` describes this release in machine-readable form.
EOF
)"
blurb="${blurb//@BIN@/$bin_name}"
blurb="${blurb//@SHA256@/$sha256}"
blurb="${blurb//@MD5@/$md5}"

{
    if [ "$is_rc" = 1 ]; then
        echo "> **Pre-release: not yet tested on a device.** Install it only if you are ready to go"
        echo "> back to the stock firmware (see below) should anything misbehave."
        echo
    fi
    printf '%s\n\n' "$notes_body"
    printf '%s\n' "$blurb"
} > "$notes"

gh_cmd=(gh release create "$tag" "${assets[@]}"
        --repo "$REPO" --title "Status-ESP $tag" --notes-file "$notes" --verify-tag)
if [ "$is_rc" = 1 ]; then
    gh_cmd+=(--prerelease --latest=false)
else
    gh_cmd+=(--latest)
fi

# --- Dry run: show, and stop -------------------------------------------------
if [ "$dry_run" = 1 ]; then
    echo
    echo "==> DRY RUN: release notes"
    echo "--------------------------------------------------------------------"
    cat "$notes"
    echo "--------------------------------------------------------------------"
    echo "==> DRY RUN: would run"
    printf '  git tag -a %q -m %q\n' "$tag" "Status-ESP $tag"
    printf '  git push origin %q\n' "refs/tags/$tag"
    printf '  '
    printf '%q ' "${gh_cmd[@]}"
    echo
    echo "==> DRY RUN: nothing was tagged, pushed or published."
    exit 0
fi

# --- Publish -----------------------------------------------------------------
echo "==> Tagging $tag and pushing it to origin"
git tag -a "$tag" -m "Status-ESP $tag"
if ! git push origin "refs/tags/$tag"; then
    git tag -d "$tag" >/dev/null
    die "could not push the tag $tag to origin; the local tag was removed again, nothing was published"
fi

echo "==> Creating the GitHub release"
if ! "${gh_cmd[@]}"; then
    trap - EXIT # keep the notes file so the step can be retried by hand
    {
        echo "release.sh: the tag $tag WAS pushed, but 'gh release create' failed."
        echo "Fix the problem, then retry just that step (notes kept in $notes):"
        printf '  '
        printf '%q ' "${gh_cmd[@]}"
        echo
        echo "or delete the tag (git push origin :refs/tags/$tag && git tag -d $tag) and start over."
    } >&2
    exit 1
fi
echo "==> Done: $tag"
