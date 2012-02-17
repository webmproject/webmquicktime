#!/bin/bash
##
##  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.
set -e

readonly BACKGROUND_IMAGE="Background.png"
readonly INSTALLER_DIR="$(pwd)"

build_full_install_dmg() {
  local readonly DMG_FILE="webm_quicktime_installer.dmg"
  local readonly WEBM_NAME="WebM QuickTime Installer"
  local readonly WEBM_MPKG="${WEBM_NAME}.mpkg"

  # Copy the contents of the disk image to |TEMP_DIR|.
  copy_uninstaller
  copy_xiphqt_licenses
  copy_bundle "${WEBM_MPKG}" "${TEMP_DIR}"

  # Create the disk image.
  create_dmg --window-size 720 380 --icon-size 48 \
    --background "${INSTALLER_DIR}/${BACKGROUND_IMAGE}" \
    --volname "${WEBM_NAME}" "/tmp/${DMG_FILE}" "${TEMP_DIR}"

  cleanup
  mv "/tmp/${DMG_FILE}" "${INSTALLER_DIR}"
}

build_webm_update_dmg() {
  local readonly DMG_FILE="webm_quicktime_updater.dmg"
  local readonly WEBM_NAME="WebM QuickTime Updater"
  local readonly WEBM_UPDATE_PKG="${WEBM_NAME}.pkg"

  # Copy the contents of the disk image to |TEMP_DIR|.
  copy_uninstaller
  copy_bundle "${WEBM_UPDATE_PKG}" "${TEMP_DIR}"

  # Create the disk image.
  create_dmg --window-size 720 380 --icon-size 48 \
    --background "${INSTALLER_DIR}/${BACKGROUND_IMAGE}" \
    --volname "${WEBM_NAME}" "/tmp/${DMG_FILE}" "${TEMP_DIR}"

  cleanup
  mv "/tmp/${DMG_FILE}" "${INSTALLER_DIR}"
}

build_xiphqt_update_dmg() {
  local readonly DMG_FILE="xiphqt_updater.dmg"
  local readonly XIPHQT_NAME="XiphQT Updater"
  local readonly XIPHQT_UPDATE_PKG="${XIPHQT_NAME}.pkg"

  # Copy the contents of the disk image to |TEMP_DIR|.
  copy_uninstaller
  copy_xiphqt_licenses
  copy_bundle "${XIPHQT_UPDATE_PKG}" "${TEMP_DIR}"

  # Create the disk image.
  create_dmg --window-size 720 380 --icon-size 48 \
    --background "${INSTALLER_DIR}/${BACKGROUND_IMAGE}" \
    --volname "${XIPHQT_NAME}" "/tmp/${DMG_FILE}" "${TEMP_DIR}"

  cleanup
  mv "/tmp/${DMG_FILE}" "${INSTALLER_DIR}"
}

cleanup() {
  local readonly RM="rm -r -f"
  ${RM} "${TEMP_DIR}"*
}

copy_uninstaller() {
  local readonly UNINSTALL_APP="uninstall.app"
  local readonly UNINSTALL_SCRIPT="scripts/uninstall_helper.sh"
  copy_bundle "${UNINSTALL_APP}" "${TEMP_DIR}"
  cp -p "${UNINSTALL_SCRIPT}" "${TEMP_DIR}"
}

copy_xiphqt_licenses() {
  local readonly XIPHQT_LICENSE_PATH="../third_party/xiphqt/"
  cp -p "${XIPHQT_LICENSE_PATH}"/*.txt "${TEMP_DIR}"
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

create_dmg() {
  local readonly CREATE_DMG_PATH="../third_party/yoursway-create-dmg/"
  local readonly CREATE_DMG="./create-dmg"

  # Note: must cd into |CREATE_DMG_PATH| for create-dmg to work.
  local readonly OLD_DIR="$(pwd)"
  cd "${CREATE_DMG_PATH}"
  ${CREATE_DMG} "$@"
  cd "${OLD_DIR}"
}

dbglog() {
  echo "build_dmg: $@"
}

# Create temporary directory.
readonly TEMP_DIR="$(mktemp -d /tmp/webmqt_dmg.XXXXXX)/"

if [[ -z "${TEMP_DIR}" ]] || [[ "{TEMP_DIR}" == "/" ]]; then
  # |TEMP_DIR| will be passed to "rm -r -f" in |cleanup|. Avoid any possible
  # mktemp shenanigans.
  dbglog "ERROR, TEMP_DIR path empty or unsafe (TEMP_DIR=${TEMP_DIR})."
  exit 1
fi

if [[ ! -e "${UNINSTALL_APP}" ]]; then
  scripts/build/build_uninstaller.sh
fi

if [[ -z "$1" ]] || [[ "$1" == "all" ]]; then
  build_full_install_dmg
  build_webm_update_dmg
  build_xiphqt_update_dmg
elif [[ "$1" == "webm" ]]; then
  build_webm_update_dmg
elif [[ "$1" == "xiph" ]]; then
  build_xiphqt_update_dmg
fi

dbglog "Done."
