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

dbglog() {
  echo "build_installers: $@"
}

readonly PKGMAKER="/Developer/usr/bin/packagemaker"
if [[ ! -e "${PKGMAKER}" ]]; then
  dbglog "ERROR, ${PKGMAKER} does not exist."
  exit 1
fi

build_installer() {
  local readonly PMDOC="$1"
  local readonly PACKAGE="$2"
  local readonly RM="rm -r -f"

  # Confirm the pmdoc exists.
  if [[ ! -e "${PMDOC}" ]]; then
    dbglog "ERROR, PMDOC ${PMDOC} does not exist!"
    exit 1
  fi

  # Delete the output package if it exists.
  if [[ -e "${PACKAGE}" ]]; then
    ${RM} "${PACKAGE}"
  fi

  # Build the package.
  ${PKGMAKER} -d "${PMDOC}" -o "${PACKAGE}"

  # Confirm that the package was built.
  if [[ ! -e "${PACKAGE}" ]]; then
    dbglog "ERROR, build failed for PACKAGE ${PACKAGE}!"
    exit 1
  fi

  dbglog "${PACKAGE} build successful."
}

# WebM components installer project and output package file names.
readonly WEBM_PMDOC="webm_component_install.pmdoc"
readonly WEBM_PACKAGE="webm_component_installer.pkg"

# XiphQT components installer project and output package file names.
readonly XIPHQT_PMDOC="xiphqt_component_install.pmdoc"
readonly XIPHQT_PACKAGE="xiphqt_component_installer.pkg"

# Full installer project and output package file names.
readonly INSTALLER_PMDOC="installer.pmdoc"
readonly INSTALLER_PACKAGE="WebM QuickTime Installer.mpkg"

build_installer "${WEBM_PMDOC}" "${WEBM_PACKAGE}"
build_installer "${XIPHQT_PMDOC}" "${XIPHQT_PACKAGE}"

# Nice long note about PackageMaker manual build requirements. Why, oh why,
# does the stupid thing have to crash _every_ time when used on the command
# line.
dbglog "${INSTALLER_PMDOC} must be opened in PackageMaker and built manually"
dbglog "to create ${INSTALLER_PACKAGE}. PackageMaker will always crash when"
dbglog "${INSTALLER_PMDOC} is built from the command line."

dbglog "Done."
