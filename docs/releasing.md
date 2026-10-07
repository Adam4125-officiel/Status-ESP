# Versioning and releases

This page describes how versions are numbered, what a release contains, and how to publish
one. Users who only want to install a release should read the
[README](../README.md#installation) instead.

## Versioning policy
Versions look like `X.Y.Z`:

| Part | Changes when | Frequency |
|---|---|---|
| **X** (major) | The whole system changes (a complete change of approach). | Very rare. Not expected in the foreseeable future. |
| **Y** (minor) | Features are added. | Frequent, especially early on. |
| **Z** (patch) | Only bug fixes, security fixes and performance fixes. **No new features.** | As needed. |

If a change adds a feature, it bumps Y, even if it also fixes bugs. A Z release never adds
anything the user could call a feature.

## Pre-releases and stable releases
Every version goes through two stages.

- **Pre-release `vX.Y.Z-rc.N`** (release candidate). Published automatically by the agent or
  maintainer at the end of a batch of work, marked "Pre-release" on GitHub. It has been built
  and has passed the automated checks, but it has **not been tested on the device**. If a
  tester finds a problem, the fix goes on the same branch and the next candidate is
  `-rc.(N+1)` of the same `X.Y.Z`.
- **Stable `vX.Y.Z`**. Published only after the project owner confirms that a release
  candidate was tested end to end on a real device and is stable. A stable release is a
  normal GitHub release (not marked pre-release) and becomes the "latest" release.

Uploading any firmware to the project owner's device always needs the owner's explicit
go-ahead, including a release candidate of a new version.

## The `VERSION` file and tags
- `VERSION` at the repository root is the **single source of truth**. It holds one line, with
  no leading `v`: `MAJOR.MINOR.PATCH` or `MAJOR.MINOR.PATCH-rc.N` (for example `0.3.0` or
  `0.3.0-rc.1`). Valid values match `^[0-9]+\.[0-9]+\.[0-9]+(-rc\.[0-9]+)?$`.
- `tools/version.py`, a PlatformIO pre-build script, reads and validates it and defines
  `FW_VERSION` for the compiler. An invalid `VERSION` fails the build with a clear message.
  Nothing else carries the version: do not hard-code it anywhere.
- `src/config.h` builds the full name `Status-ESP-<version>` from it. This is what `/v.json`
  reports and what appears in the binary; `tools/make_release.py` checks that exact string is
  inside the image.
- The git tag is **`v` + `VERSION`** (`v0.3.0-rc.1`, `v0.3.0`). `tools/release.sh` creates it;
  do not create release tags by hand.
- Sanity check after tagging: `git show v<version>:VERSION` must print `<version>`.

## Release assets
`tools/make_release.py` writes these files into `dist/v<version>/` (the `dist/` directory is
git-ignored), and `tools/release.sh` attaches all of them to the GitHub release:

| File | Purpose |
|---|---|
| `Status-ESP-<version>.bin` | The firmware image to upload to the device's `/update` page. |
| `version.json` | Machine-readable description of the release (schema below). |
| `checksums.txt` | MD5 and SHA-256 of the `.bin`, in BSD tag format. |

There is no zip file: users download the `.bin` directly.

`checksums.txt` looks like this and can be verified with `cksum -c checksums.txt` (GNU
coreutils 9 or newer), or by hand with `Get-FileHash` on Windows:
```
MD5 (Status-ESP-0.3.0-rc.1.bin) = <hex>
SHA256 (Status-ESP-0.3.0-rc.1.bin) = <hex>
```

### `version.json`
```json
{"name":"Status-ESP","version":"0.3.0-rc.1","tag":"v0.3.0-rc.1","prerelease":true,
 "target":"smalltv-ultra","file":"Status-ESP-0.3.0-rc.1.bin","size":376000,
 "md5":"<hex>","sha256":"<hex>",
 "url":"https://github.com/Adam4125-officiel/Status-ESP/releases/download/v0.3.0-rc.1/Status-ESP-0.3.0-rc.1.bin"}
```

| Field | Meaning |
|---|---|
| `name` | Project name, `Status-ESP`. |
| `version` | The contents of `VERSION`. |
| `tag` | The git tag, `v` + `version`. |
| `prerelease` | `true` when the version contains `-rc.`, otherwise `false`. |
| `target` | The PlatformIO environment the image was built for (`smalltv-ultra`). |
| `file` | File name of the `.bin` in the same release. |
| `size` | Size of the `.bin` in bytes. |
| `md5` | Lowercase hex MD5 (the ESP8266 `Updater` class's `setMD5()` takes this form). |
| `sha256` | Lowercase hex SHA-256. |
| `url` | Direct download URL of the `.bin`. |

`version.json` is groundwork for a **future on-device auto-updater, which does not exist
yet**. The idea is that the device reads
`https://github.com/Adam4125-officiel/Status-ESP/releases/latest/download/version.json`,
which is why the file has a fixed name. GitHub's "latest" release only ever resolves to a
**stable** release, so a pre-release never reaches a device automatically. That URL only
works for anonymous clients while the repository is public, and returns 404 until the first
stable release exists.

## Release procedure
Releases are published from the development machine with `tools/release.sh`, which needs
`git`, the project's local toolchain (`bash tools/setup.sh`) and the authenticated GitHub CLI
(`gh auth status`).

The script refuses to run unless all of these hold:
- the working tree is clean;
- `VERSION` is valid;
- the tag `v<version>` does not exist yet;
- `origin` is the Status-ESP repository, and `HEAD` has been pushed (it equals its upstream
  branch);
- `CHANGELOG.md` has a non-empty `## [<version>]` section (it becomes the release notes);
- for a stable version (no `-rc.`), the current branch is `main`.

It lists every problem it finds in one run, rather than stopping at the first.

It then builds the firmware (`tools/build.sh`), runs `tools/make_release.py`, creates the
annotated tag `v<version>`, pushes it, and runs `gh release create` with the three assets.
The release notes are the matching `CHANGELOG.md` section plus a short install and
verification note (and "not yet tested on the device" for a pre-release). Pre-releases are
created with `--prerelease --latest=false`.

### Publishing a pre-release (end of a batch of work)
1. On the version's branch (for example `0.3.0`), make sure `VERSION` is `X.Y.Z-rc.N` and
   that `CHANGELOG.md` has a `## [X.Y.Z-rc.N] - <date>` section describing the changes.
2. Commit and push the branch (a draft pull request into `main` stays open).
3. Rehearse: `bash tools/release.sh --dry-run`. This builds, produces the assets and prints
   the release notes, and does nothing remote.
4. Publish: `bash tools/release.sh`.
5. Tell the owner exactly what to test on the device.

### Promoting to stable
Only after the owner says the release candidate was tested end to end on the device and is
stable:
1. Merge the pull request into `main` with a **regular merge commit**
   (`gh pr merge <number> --merge`), never squash or rebase. Squashing would collapse the
   one-commit-per-change history that `git bisect` and `git revert` rely on.
2. On `main`, bump `VERSION` to `X.Y.Z` (no `-rc.N`) and update `CHANGELOG.md`: add the
   `## [X.Y.Z] - <date>` section (see below), commit, and push `main`.
3. `bash tools/release.sh --dry-run`, then `bash tools/release.sh`. The script insists on
   being on `main` for a stable version.
4. Check that the release exists and is marked "Latest", that
   `git show vX.Y.Z:VERSION` prints `X.Y.Z`, and that
   `https://github.com/Adam4125-officiel/Status-ESP/releases/latest/download/version.json`
   now resolves.
5. Delete the merged branch (remote and local).

A documentation-only fix with no version branch open may go straight to `main` without a
release.

## What CI does and does not do
The workflow in `.github/workflows/build.yml` runs on every push and every pull request. It
installs PlatformIO, builds the firmware, enforces the size limit, runs
`tools/make_release.py` to validate the image, and uploads `dist/` as a workflow artifact.

It does **not** publish releases or create tags. Publishing is a deliberate step done with
`tools/release.sh`.

## Writing `CHANGELOG.md` entries
- Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), in English, newest
  version first, with headings like `## [0.3.0-rc.1] - 2026-10-05`.
- Group entries under `### Added`, `### Changed`, `### Fixed` and `### Removed`, and leave out
  empty groups.
- Write for a user reading the release page, not for a developer reading a diff: say what is
  different, not which function changed.
- Every version that gets published, release candidates included, needs its own
  `## [<version>]` section before `tools/release.sh` will run.
- When a stable version is promoted, fold its release candidates into one
  `## [X.Y.Z] - <date>` section that lists everything since the previous stable release, and
  remove the `-rc.N` sections. The release candidates' own notes remain on their GitHub
  releases.
- A change that touches the device's behaviour, an on-disk file or a setting should say so,
  including anything that survives or does not survive an update.
