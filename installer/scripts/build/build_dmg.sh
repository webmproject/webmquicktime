#!/bin/bash
##
##  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.

readonly BACKGROUND_IMAGE="Background.png"
readonly CREATE_DMG_PATH="../third_party/yoursway-create-dmg/"
readonly CREATE_DMG="./create-dmg"
readonly DMG_FILE="webm_quicktime_installer.dmg"
readonly INSTALLER_DIR="$(pwd)"
readonly UNINSTALL_APP="uninstall.app"
readonly UNINSTALL_SCRIPT="scripts/uninstall_helper.sh"
readonly WEBM_NAME="WebM QuickTime Installer"
readonly WEBM_MPKG="${WEBM_NAME}.mpkg"
readonly XIPHQT_LICENSE_PATH="../third_party/xiphqt/"

dbglog() {
  echo "build_dmg: $@"
}

copy_bundle() {
  local readonly BUNDLE="$1"
  local readonly TARGET="$2"

  if [[ -z "${BUNDLE}" ]] || [[ ! -e "${BUNDLE}" ]]; then
    dbglog "ERROR, invalid bundle path passed to copy_bundle, quitting."
    dbglog "bundle path=${BUNDLE}"
    exit 1
  fi

  if [[ -z "${TARGET}" ]]; then
    dbglog "ERROR, invalid target path passed to copy_bundle, quitting."
    dbglog "target path=${TARGET}"
    exit 1
  fi

  local readonly COPY="cp -p -R"
  ${COPY} "${BUNDLE}" "${TARGET}"
}

# Create temporary directory.
readonly TEMP_DIR="$(mktemp -d /tmp/webmqt_dmg.XXXXXX)/"

if [[ ! -e "${UNINSTALL_APP}" ]]; then
  scripts/build/build_uninstaller.sh
fi

# Copy the contents of the disk image to |TEMP_DIR|.
copy_bundle "${UNINSTALL_APP}" "${TEMP_DIR}"
copy_bundle "${WEBM_MPKG}" "${TEMP_DIR}"
cp -p "${XIPHQT_LICENSE_PATH}"/*.txt "${TEMP_DIR}"
cp -p "${UNINSTALL_SCRIPT}" "${TEMP_DIR}"

# Create the disk image.
# Note: have to cd into |CREATE_DMG_PATH| for create-dmg to work.
cd "${CREATE_DMG_PATH}"
${CREATE_DMG} --window-size 720 380 --icon-size 48 \
  --background "${INSTALLER_DIR}/${BACKGROUND_IMAGE}" \
  --volname "${WEBM_NAME}" "/tmp/${DMG_FILE}" "${TEMP_DIR}"

readonly RM="rm -r -f"
${RM} "${TEMP_DIR}"
mv "/tmp/${DMG_FILE}" "${INSTALLER_DIR}"
