# Releasing

How the packages of Silent Heritage are built, signed and published, the
one-time setup it relies on, and the steps of each release.

## How it works

Everything is done by the repository's workflows (`.github/workflows`):

- `ci.yml`, on the pushes to `main` and the pull requests: the tests (debug
  build with the sanitizers, both variants) and every package built as a
  check (`build.yml`). It has no secret.
- `release.yml`, on a pushed tag `vMAJOR.MINOR.PATCH[-suffix]`:
  1. checks the tag;
  2. builds every package, unsigned (`build.yml`, no secret);
  3. signs the Android packages in a job of the `release` environment,
     the only one with the release key, which runs no build
     (`android/sign_apk.sh`);
  4. creates a **draft** release: the packages, `SHA256SUMS`, GitHub's build
     provenance attestations of every file, the notes from the version's
     section of `CHANGELOG.md` (empty without one: write them in the draft)
     and the Android version code.
  Nothing is public until the draft is published by hand.

The packages, for the classic (`SilentHeritage-…`) and the extended
(`SilentHeritageExtended-…`) variants:

| Platform | File | Built by |
|---|---|---|
| Android 7.0+ (arm64-v8a, x86_64) | `…-<version>-android.apk` | `android/build_apk.sh --unsigned`, then `android/sign_apk.sh` |
| Linux x86_64, aarch64 (glibc 2.35+) | `…-<version>-linux-<arch>.tar.gz`, `…-<version>-linux-<arch>.AppImage` | `packaging/build_desktop.sh linux`, in Ubuntu 22.04 |
| Windows x64 | `…-<version>-windows-x64.zip` | `packaging/build_desktop.sh windows` (mingw-w64 cross build) |
| macOS 13.3+ (Apple Silicon, Intel) | `…-<version>-macos.dmg` | `packaging/build_desktop.sh macos`, on macOS 15 (the icons made on Linux) |

Versions: the version name is the tag without its `v`; the Android version
code is `release.yml`'s run number, so that it grows with every release,
pre-releases included. Keep the workflow's file name: a renamed workflow
counts from 1 again. Re-running a failed release keeps its run number.

The Silent Hunter edition (`SH_EDITION=hunter`, the original game files
embedded) is never built by a workflow: CMake and `android/build_apk.sh`
refuse it when the `CI` environment variable is set.

## One-time setup

### 1. The Android release key

Every update of an Android app must be signed with the same key as the
installed one: losing the key means no more updates (only a new app), and
anyone holding it can sign an "update" users will accept. Make it on your
own computer:

    keytool -genkeypair -keystore silentheritage-release.p12 -storetype PKCS12 \
      -alias silentheritage -keyalg RSA -keysize 4096 -validity 18250 \
      -dname "CN=<your name>, O=Conning Tower"

with a long random password (e.g. `openssl rand -base64 24`); a PKCS12
keystore has one password for the store and the key. 50 years of validity:
Google Play wants a key valid past 2033. Note its fingerprint, which users
and stores check:

    keytool -list -v -keystore silentheritage-release.p12

(it asks for the password; the fingerprint is the `SHA256:` line of
"Certificate fingerprints").

Keep two offline copies of the keystore (e.g. an encrypted USB key and a
password manager's file attachment) and the password in your password
manager. The keystore never goes into the repository (`.gitignore` excludes
`*.p12`, `*.keystore`, `*.jks`).

### 2. The `release` environment

Settings → Environments → New environment, named `release`:

- Deployment branches and tags → Selected branches and tags → Add
  deployment branch or tag rule: type **Tag**, pattern `v*`. Only the
  release workflow of a `v*` tag can use its secrets.
- No required reviewers: a release stops as a draft anyway.
- Environment secrets:
  - `ANDROID_KEYSTORE_BASE64`: the keystore in base64,
    `base64 -w0 silentheritage-release.p12` (on macOS `base64 -i …`);
  - `ANDROID_KEY_ALIAS`: `silentheritage`;
  - `ANDROID_KEYSTORE_PASSWORD`: its password;
  - `ANDROID_KEY_PASSWORD`: only for a key whose password differs (not
    with PKCS12).

Do not add the key as a repository secret: those reach every workflow.

### 3. The repository's settings

- Settings → Rules → Rulesets → New ruleset → New tag ruleset: name
  `release tags`, enforcement Active, target tags matching `v*`, rules
  "Restrict creations", "Restrict updates" and "Restrict deletions", bypass
  list "Repository admin". Only you can create, move or delete a release
  tag. (Rulesets on a private repository need a paid plan; they are free
  on a public one.)
- Settings → Actions → General:
  - Actions permissions: "Allow conning-tower, and select non-conning-tower,
    actions and reusable workflows" with "Allow actions created by GitHub"
    (the workflows use only `actions/*`, pinned to a commit);
  - Fork pull request workflows: "Require approval for all external
    contributors";
  - Workflow permissions: "Read repository contents and packages
    permissions" (the workflows ask for more where they need it), "Allow
    GitHub Actions to create and approve pull requests" unchecked.
- Settings → Advanced Security (Code security): Dependabot alerts and
  security updates, secret scanning and push protection on. Dependabot
  proposes the actions' new versions monthly (`.github/dependabot.yml`).
- The Linux aarch64 packages build on GitHub's `ubuntu-24.04-arm` runners,
  free for public repositories. While the repository is private, that job
  may wait for a runner that never comes: make the repository public
  before the first release.
- Your account: two-factor authentication (a release only needs a push of a
  tag: whoever holds your account can release).

### 4. Android developer verification

Google requires apps installed outside Google Play on certified devices to
come from a verified developer (from September 2026 in Brazil, Indonesia,
Singapore and Thailand, then elsewhere). Your verified Google Play Console
account covers the identity. Register the packages
`com.conningtower.silentheritage` and
`com.conningtower.silentheritage.extended` with the release key's SHA-256
fingerprint, from the Play Console (Android developer verification). Proving
that you hold the key means signing what the console asks for with it, on
your computer. Google's steps change; follow the console.

## A release

1. In `CHANGELOG.md`, add the version's section at the top, e.g.
   `## [1.0.0] - 2026-11-02`, with its changes: the release's notes. Without
   one, the draft's notes are empty.
2. Commit and push to `main`; wait for `ci` to pass.
3. Tag and push the tag:

       git tag -a v1.0.0 -m "Silent Heritage 1.0.0"
       git push origin v1.0.0

4. Follow the `release` run in the Actions tab (about 20 minutes).
5. On the Releases page, open the draft and check it: the files (2 APKs, 4
   tarballs, 4 AppImages, 2 zips, 2 disk images, `SHA256SUMS`), the notes, the
   pre-release mark for a suffixed version. To check an APK's signature:
   `apksigner verify --print-certs <apk>` shows the release key's SHA-256.
6. Publish.
7. Homebrew: in [conning-tower/homebrew-tap](https://github.com/conning-tower/homebrew-tap),
   set `Formula/silent-heritage.rb`'s `url` to the release's tag and its
   commit (`tag: "v<version>"`, `revision:` the output of
   `git rev-parse v<version>^{commit}`), in a pull request: the tap's
   workflow builds it on a Mac.

A failed run: fix the cause, then either re-run the failed jobs (same
version code), or delete the tag (`git push origin :v1.0.0`, `git tag -d
v1.0.0`) and the draft if one was made, and tag again (new version code). A
re-run of a release whose draft exists replaces its files.

## Code signing policy

The policy that a code signing service (SignPath Foundation for the Windows
executables, once accepted) asks for.

- The signed files are built by the release workflow from the repository's
  sources at the release's tag, on GitHub's runners; their build provenance
  is attested by GitHub.
- Authors, reviewers and approvers: Mathias Kende, the only committer and
  the only one who can push release tags. Contributions from others come
  as pull requests, reviewed before they are merged.
- The release key is held by the maintainer and the repository's
  `release` environment only; it signs only the release workflow's
  packages.
- Privacy: the programs collect nothing and connect to nothing.

## Later

- Windows code signing: apply to the SignPath Foundation's free programme
  for open-source projects (https://signpath.org) once the repository is
  public and has a release. When accepted, a signing job submitting the
  `.exe` files to SignPath, with its API token in the `release`
  environment, goes before the zips are made.
- F-Droid: the Android packages are reproducible (the same sources, SDK,
  NDK and JDK give the same unsigned package, from a clean checkout:
  `build_apk.sh` reuses the SDL sources of a `build/linux` directory, whose
  path then differs in the native library's build ID), so that F-Droid can
  publish them with this signature. Its recipe (in the fdroiddata repository) runs
  `android/build_apk.sh --unsigned` with the release's `SH_VERSION_NAME`
  and `SH_VERSION_CODE` (in the release's notes) and `SOURCE_DATE_EPOCH` of
  the tag's commit.
- Google Play: an Android App Bundle (`aapt2 link --proto-format` and
  bundletool), uploaded with Play App Signing set to the release key
  (exported with Google's PEPK tool) so that Play's packages and these
  update each other.
