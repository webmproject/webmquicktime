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

if [[ $(basename $(pwd)) != "installer" ]] || \
    [[ $(basename $(dirname $(pwd))) != "webmquicktime" ]]; then
  echo "$(basename $0) must be run from webmquicktime/installer"
  exit 1
fi

source scripts/build/util.sh
readonly BACKGROUND_IMAGE="Background.png"
readonly INSTALLER_DIR="$(pwd)"

file_exists "${BACKGROUND_IMAGE}" || die "${BACKGROUND_IMAGE} does not exist."

## build_dmg <DMG file name> <Volume name> <Package file>
##     [Include Xiph Licenses]
## For example, the following command:
##   build_dmg widget.dmg "Awesome Widgets" awesome_widgets.pkg
## The above builds a DMG file named widget.dmg that is mounted as a volume
## named "Awesome Widgets", and contains:
##   - awesome_widgets.pkg
##   - uninstaller.app
##   - uninstall_helper.sh
## When a fourth argument is present, |build_dmg| includes the XiphQT
## COPYING.*.txt files in the disk image.
build_dmg() {
  local readonly DMG_FILE="$1"
  local readonly VOL_NAME="$2"
  local readonly PKG_FILE="$3"
  local readonly COPY_XIPH_LICENSES="$4"

  if [[ -z "${DMG_FILE}" ]]; then
    die "${FUNCNAME}: DMG file name empty."
  fi

  if [[ -z "${VOL_NAME}" ]]; then
    die "${FUNCNAME}: Volume name empty."
  fi

  if [[ -z "${PKG_FILE}" ]]; then
    die "${FUNCNAME}: package file name empty."
  fi

  file_exists "${PKG_FILE}" || die "${PKG_FILE} does not exist."
  copy_uninstaller

  if [[ -n "${COPY_XIPH_LICENSES}" ]]; then
    # Copy the XiphQT COPYING.*.txt files.
    copy_xiphqt_licenses
  fi

  copy_bundle "${PKG_FILE}" "${TEMP_DIR}"

  # Create the disk image.
  create_dmg --window-size 720 380 --icon-size 48 \
    --background "${INSTALLER_DIR}/${BACKGROUND_IMAGE}" \
    --volname "${VOL_NAME}" "/tmp/${DMG_FILE}" "${TEMP_DIR}"

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

create_dmg() {
  local readonly CREATE_DMG_PATH="../third_party/yoursway-create-dmg/"
  local readonly CREATE_DMG="./create-dmg"

  # Note: must cd into |CREATE_DMG_PATH| for create-dmg to work.
  local readonly OLD_DIR="$(pwd)"
  cd "${CREATE_DMG_PATH}"
  ${CREATE_DMG} "$@"
  cd "${OLD_DIR}"
}

# Create temporary directory.
readonly TEMP_DIR="$(mktemp -d /tmp/webmqt_dmg.XXXXXX)/"

if [[ -z "${TEMP_DIR}" ]] || [[ "{TEMP_DIR}" == "/" ]]; then
  # |TEMP_DIR| will be passed to "rm -r -f" in |cleanup|. Avoid any possible
  # mktemp shenanigans.
  die "TEMP_DIR path empty or unsafe (TEMP_DIR=${TEMP_DIR})."
fi

if [[ ! -e "${UNINSTALL_APP}" ]]; then
  scripts/build/build_uninstaller.sh
fi

readonly WEBM_DMG_FILE="webm_quicktime_installer.dmg"
readonly WEBM_NAME="WebM QuickTime Installer"
readonly WEBM_MPKG="${WEBM_NAME}.mpkg"
readonly WEBM_UPDATE_DMG_FILE="webm_quicktime_updater.dmg"
readonly WEBM_UPDATE_NAME="WebM QuickTime Updater"
readonly WEBM_UPDATE_PKG="${WEBM_UPDATE_NAME}.pkg"
readonly XIPHQT_DMG_FILE="xiphqt_updater.dmg"
readonly XIPHQT_NAME="XiphQT Updater"
readonly XIPHQT_PKG="${XIPHQT_NAME}.pkg"

if [[ -z "$1" ]] || [[ "$1" == "all" ]]; then
  #build_full_install_dmg
  #build_webm_update_dmg
  #build_xiphqt_update_dmg
  debuglog "Building all disk images."
  build_dmg "${WEBM_DMG_FILE}" "${WEBM_NAME}" "${WEBM_MPKG}" xiph
  build_dmg "${XIPHQT_DMG_FILE}" "${XIPHQT_NAME}" "${XIPHQT_PKG}" xiph
  build_dmg "${WEBM_UPDATE_DMG_FILE}" "${WEBM_UPDATE_NAME}" \
      "${WEBM_UPDATE_PKG}"
elif [[ "$1" =~ webm ]]; then
  #build_webm_update_dmg
  debuglog "Building WebM update disk image."
  build_dmg "${WEBM_UPDATE_DMG_FILE}" "${WEBM_UPDATE_NAME}" \
      "${WEBM_UPDATE_PKG}"
elif [[ "$1" =~ xiph ]]; then
  #build_xiphqt_update_dmg
  debuglog "Building XiphQT update disk image."
  build_dmg "${XIPHQT_DMG_FILE}" "${XIPHQT_NAME}" "${XIPHQT_PKG}" xiph
fi

debuglog "Done."
