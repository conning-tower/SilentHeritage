#!/usr/bin/env bash
# Signs an unsigned package of android/build_apk.sh --unsigned with the
# release key, and checks the signature: APK signature schemes v2 and v3
# (the packages need Android 7.0, API 24, which reads v2; v3 allows a key
# rotation), no v1 (JAR) signature. The release workflow's signing job runs
# it (.github/workflows/release.yml); it builds nothing.
#
# Usage: android/sign_apk.sh <unsigned.apk> <signed.apk>
# Environment:
#   ANDROID_HOME  the Android SDK, holding build-tools/36.1.0 (required;
#                 ANDROID_SDK_ROOT is accepted too)
#   SH_RELEASE_KEYSTORE           the release keystore (PKCS12) (required)
#   SH_RELEASE_KEY_ALIAS          the key's alias in it (required)
#   SH_RELEASE_KEYSTORE_PASSWORD  the keystore's password (required)
#   SH_RELEASE_KEY_PASSWORD       the key's password [the keystore's]
# The passwords are passed to apksigner by the environment, never on its
# command line.
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <unsigned.apk> <signed.apk>" >&2
  exit 1
fi
readonly in="$1" out="$2"
readonly sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
readonly apksigner="${sdk}/build-tools/36.1.0/apksigner"
for var in SH_RELEASE_KEYSTORE SH_RELEASE_KEY_ALIAS SH_RELEASE_KEYSTORE_PASSWORD; do
  if [[ -z "${!var:-}" ]]; then
    echo "sign_apk.sh: set ${var}" >&2
    exit 1
  fi
done
export SH_RELEASE_KEY_PASSWORD="${SH_RELEASE_KEY_PASSWORD:-${SH_RELEASE_KEYSTORE_PASSWORD}}"
if [[ -z "${sdk}" || ! -x "${apksigner}" ]]; then
  echo "sign_apk.sh: set ANDROID_HOME to an Android SDK with build-tools/36.1.0" >&2
  exit 1
fi

"${apksigner}" sign --ks "${SH_RELEASE_KEYSTORE}" --ks-type PKCS12 --ks-key-alias "${SH_RELEASE_KEY_ALIAS}" \
  --ks-pass env:SH_RELEASE_KEYSTORE_PASSWORD --key-pass env:SH_RELEASE_KEY_PASSWORD \
  --v1-signing-enabled false --v2-signing-enabled true --v3-signing-enabled true \
  --out "${out}" "${in}"
"${apksigner}" verify --verbose --print-certs "${out}"
