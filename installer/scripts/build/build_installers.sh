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

show_full_installer_warning() {
  # Full installer project and output package file names.
  local readonly INSTALLER_PMDOC="installer.pmdoc"
  local readonly INSTALLER_PACKAGE="WebM QuickTime Installer.mpkg"

  # Nice long note about PackageMaker manual build requirements. Why, oh why,
  # does the stupid thing have to crash _every_ time when used on the command
  # line.
  dbglog "${INSTALLER_PMDOC} must be opened in PackageMaker and built manually"
  dbglog "to create ${INSTALLER_PACKAGE}. PackageMaker will always crash when"
  dbglog "${INSTALLER_PMDOC} is built from the command line."
}

# WebM components installer project and output package file names.
readonly WEBM_INSTALLER_PMDOC="webm_component_install.pmdoc"
readonly WEBM_INSTALLER_PACKAGE="webm_component_installer.pkg"

# WebM components updater project and output package file names.
readonly WEBM_UPDATER_PMDOC="webm_component_update.pmdoc"
readonly WEBM_UPDATER_PACKAGE="WebM QuickTime Updater.pkg"

# XiphQT components installer project and output package file names.
readonly XIPHQT_INSTALLER_PMDOC="xiphqt_component_install.pmdoc"
readonly XIPHQT_INSTALLER_PACKAGE="xiphqt_component_installer.pkg"

# XiphQT components updater project and output package file names.
readonly XIPHQT_UPDATER_PMDOC="xiphqt_component_update.pmdoc"
readonly XIPHQT_UPDATER_PACKAGE="XiphQT Updater.pkg"

if [[ -z "$1" ]] || [[ "$1" == "all" ]]; then
  build_installer "${WEBM_INSTALLER_PMDOC}" "${WEBM_INSTALLER_PACKAGE}"
  build_installer "${WEBM_UPDATER_PMDOC}" "${WEBM_UPDATER_PACKAGE}"
  build_installer "${XIPHQT_INSTALLER_PMDOC}" "${XIPHQT_INSTALLER_PACKAGE}"
  build_installer "${XIPHQT_UPDATER_PMDOC}" "${XIPHQT_UPDATER_PACKAGE}"
  show_full_installer_warning
elif [[ "$1" == "full" ]]; then
  build_installer "${WEBM_INSTALLER_PMDOC}" "${WEBM_INSTALLER_PACKAGE}"
  build_installer "${XIPHQT_INSTALLER_PMDOC}" "${XIPHQT_INSTALLER_PACKAGE}"
  show_full_installer_warning
elif [[ "$1" == "webm" ]]; then
  build_installer "${WEBM_INSTALLER_PMDOC}" "${WEBM_INSTALLER_PACKAGE}"
  build_installer "${WEBM_UPDATER_PMDOC}" "${WEBM_UPDATER_PACKAGE}"
elif [[ "$1" == "xiph" ]]; then
  build_installer "${XIPHQT_INSTALLER_PMDOC}" "${XIPHQT_INSTALLER_PACKAGE}"
  build_installer "${XIPHQT_UPDATER_PMDOC}" "${XIPHQT_UPDATER_PACKAGE}"
fi

dbglog "Done."
