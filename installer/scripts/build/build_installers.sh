#!/bin/bash
##
##  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.

# Builds the WebM QuickTime and XiphQT component installers, and then
# includes each package in one final mpkg along with
# GoogleSoftwareUpdate.pkg
set -e

if [[ $(basename $(pwd)) != "installer" ]] || \
    [[ $(basename $(dirname $(pwd))) != "webmquicktime" ]]; then
  echo "$(basename $0) must be run from webmquicktime/installer"
  exit 1
fi

source scripts/build/util.sh

readonly PKGMAKER="/usr/local/bin/packagesbuild"
file_exists "${PKGMAKER}" || die "${PKGMAKER} does not exist."

build_installer() {
  local readonly PKGPROJ="$1"
  local readonly PACKAGE="$2"
  local readonly RM="rm -r -f"

  # Confirm the pkgproj file exists.
  file_exists "${PKGPROJ}" || die "${PKGPROJ} does not exist!"

  # Delete the output package if it exists.
  if [[ -e "${PACKAGE}" ]]; then
    ${RM} "${PACKAGE}"
  fi

  # Build the package.
  ${PKGMAKER} -d "${PKGPROJ}"

  # Confirm that the package was built.
  file_exists "${PACKAGE}"|| die "${PACKAGE} build failed."

  debuglog "${PACKAGE} build successful."
}

# WebM components installer project and output package file names.
readonly WEBM_INSTALLER_PKGPROJ="webmqt_installer.pkgproj"
readonly WEBM_INSTALLER_PACKAGE="WebM QuickTime Installer.pkg"

build_installer "${WEBM_INSTALLER_PKGPROJ}" "${WEBM_INSTALLER_PACKAGE}"

debuglog "Done."
