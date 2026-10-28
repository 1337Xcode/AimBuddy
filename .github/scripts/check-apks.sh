#!/usr/bin/env bash
# Checks the two APKs of a release before it is published:
#   - both signatures verify, and both are signed with the same key;
#   - that key is the one the previous release was signed with, so the app updates in place;
#   - the packages and versions are the ones this release is meant to have;
#   - neither APK carries a detection model.
# Usage: check-apks.sh <folder with the two APKs> <version>
# Run from the repository root. Needs `gh` (logged in) to look at the previous release.
set -euo pipefail

DIR="${1:?usage: check-apks.sh <folder with the two APKs> <version>}"
VERSION="${2:?usage: check-apks.sh <folder with the two APKs> <version>}"
APP_FILE="AimBuddy-v${VERSION}.apk"
DEBUG_FILE="AimBuddy-v${VERSION}-debug.apk"
APP_APK="${DIR}/${APP_FILE}"
DEBUG_APK="${DIR}/${DEBUG_FILE}"

fail() {
  echo "::error::$*"
  exit 1
}

# Where apksigner and aapt2 are: the newest build tools in the SDK, unless the caller names them.
if [ -z "${APKSIGNER:-}" ] || [ -z "${AAPT2:-}" ]; then
  SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
  [ -n "${SDK}" ] || fail "ANDROID_HOME is not set, so apksigner and aapt2 cannot be found"
  BUILD_TOOLS=$(find "${SDK}/build-tools" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -n1)
  [ -n "${BUILD_TOOLS}" ] || fail "no build tools found under ${SDK}/build-tools"
  APKSIGNER="${APKSIGNER:-${BUILD_TOOLS}/apksigner}"
  AAPT2="${AAPT2:-${BUILD_TOOLS}/aapt2}"
fi

# The SHA-256 of the certificate an APK is signed with.
signer_of() {
  "${APKSIGNER}" verify --print-certs "$1" | sed -n 's/^Signer #1 certificate SHA-256 digest: //p'
}

# One field (name, versionCode or versionName) of the package line of an APK's manifest.
badge() {
  "${AAPT2}" dump badging "$1" | sed -n 1p | tr ' ' '\n' | sed -n "s/^$2='\\(.*\\)'\$/\\1/p"
}

expect() {
  [ "$2" = "$3" ] || fail "$1 is '$2', not '$3'"
}

for apk in "${APP_APK}" "${DEBUG_APK}"; do
  [ -f "${apk}" ] || fail "${apk} is missing"
  "${APKSIGNER}" verify "${apk}" > /dev/null || fail "the signature of $(basename "${apk}") does not verify"
done

# One key for both files.
APP_SIGNER=$(signer_of "${APP_APK}")
DEBUG_SIGNER=$(signer_of "${DEBUG_APK}")
[ -n "${APP_SIGNER}" ] || fail "${APP_FILE} has no signing certificate"
[ "${APP_SIGNER}" = "${DEBUG_SIGNER}" ] || fail "the two APKs are signed with different keys (${APP_SIGNER} and ${DEBUG_SIGNER})"

# The packages and versions this release is meant to have.
VERSION_CODE=$(sed -n 's/^aimbuddy\.versionCode=//p' gradle.properties)
expect "the package of ${APP_FILE}" "$(badge "${APP_APK}" name)" "com.aimbuddy"
expect "the package of ${DEBUG_FILE}" "$(badge "${DEBUG_APK}" name)" "com.aimbuddy.debug"
expect "the version of ${APP_FILE}" "$(badge "${APP_APK}" versionName)" "${VERSION}"
expect "the version of ${DEBUG_FILE}" "$(badge "${DEBUG_APK}" versionName)" "${VERSION}-debug"
expect "the version code of ${APP_FILE}" "$(badge "${APP_APK}" versionCode)" "${VERSION_CODE}"
expect "the version code of ${DEBUG_FILE}" "$(badge "${DEBUG_APK}" versionCode)" "${VERSION_CODE}"

# The app carries no model: a model comes from the store or from the player's own files.
# (A `.bin` outside `assets/` can be a library's own file, so only `assets/` is looked at.)
for apk in "${APP_APK}" "${DEBUG_APK}"; do
  if unzip -Z1 "${apk}" | grep -E '\.param$|^assets/.*\.bin$'; then
    fail "$(basename "${apk}") carries a detection model"
  fi
done

# The key of the release before this one: a different key means nobody could update over their copy.
PREVIOUS_TAG=$(gh release list --exclude-drafts --limit 5 --json tagName --jq "[.[].tagName | select(. != \"v${VERSION}\")][0] // empty")
PREVIOUS_NOTE="no earlier release to compare the signing key with"
if [ -n "${PREVIOUS_TAG}" ]; then
  PREVIOUS_FILE="AimBuddy-${PREVIOUS_TAG}.apk"
  # (No `grep -q` here: with pipefail, grep leaving early can fail the pipeline and be read as "not listed".)
  if gh release view "${PREVIOUS_TAG}" --json assets --jq '.assets[].name' | grep -xF "${PREVIOUS_FILE}" > /dev/null; then
    PREVIOUS_DIR=$(mktemp -d)
    gh release download "${PREVIOUS_TAG}" --pattern "${PREVIOUS_FILE}" --dir "${PREVIOUS_DIR}"
    PREVIOUS_SIGNER=$(signer_of "${PREVIOUS_DIR}/${PREVIOUS_FILE}")
    [ "${PREVIOUS_SIGNER}" = "${APP_SIGNER}" ] ||
      fail "${APP_FILE} is signed with another key than ${PREVIOUS_TAG} (${APP_SIGNER} and ${PREVIOUS_SIGNER}), so it could not be installed over it"
    PREVIOUS_NOTE="the same key as ${PREVIOUS_TAG}"
  else
    echo "::warning::${PREVIOUS_TAG} has no ${PREVIOUS_FILE}, so the signing key could not be compared with it"
    PREVIOUS_NOTE="${PREVIOUS_TAG} has no ${PREVIOUS_FILE} to compare with"
  fi
fi

echo "Both APKs verify, carry no model, and are signed with ${APP_SIGNER} (${PREVIOUS_NOTE})."
{
  echo "### APKs"
  echo
  echo "| File | Size | Package | Version |"
  echo "|------|------|---------|---------|"
  for apk in "${APP_APK}" "${DEBUG_APK}"; do
    echo "| $(basename "${apk}") | $(stat -c %s "${apk}") bytes | $(badge "${apk}" name) | $(badge "${apk}" versionName) |"
  done
  echo
  echo "Signer SHA-256: \`${APP_SIGNER}\` (${PREVIOUS_NOTE})"
} >> "${GITHUB_STEP_SUMMARY:-/dev/null}"
