#!/bin/bash
##
##  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.
readonly QT_SYS_PATH="/Library/QuickTime/"
readonly WEBM_COMPONENT="AWebM.component"
readonly WEBM_ID="org.webmproject.webmquicktime.component"
readonly XIPHQT_COMPONENT="XiphQT.component"
readonly XIPHQT_ID="org.xiph.xiph-qt.xiphqt"

dbglog() {
  logger "WebM QuickTime uninstall_helper: $@"
}

delete_component () {
  local readonly BUNDLE="$1"
  local readonly BUNDLE_ID="$2"
  if [[ -z "${BUNDLE}" ]] || [[ -z "${BUNDLE_ID}" ]]; then
    dbglog "ERROR, invalid arg(s) passed to delete_component, quitting."
    exit 1
  fi

  # Delete the bundle (if it exists).
  local readonly RM="rm -r -f"
  if [[ -e "${BUNDLE}" ]]; then
    dbglog "${RM} ${BUNDLE}"
    ${RM} "${BUNDLE}"
  else
    dbglog "${BUNDLE} does not exist."
  fi

  # Always unregister the component bundle ID; the user might have manually
  # deleted the component.
  local readonly KEYSTONE="/Library/Google/GoogleSoftwareUpdate/GoogleSoftwareUpdate.bundle/Contents/MacOS/ksadmin"
  if [[ -e "${KEYSTONE}" ]]; then
    dbglog "${KEYSTONE} --delete --productid ${BUNDLE_ID}"
    ${KEYSTONE} --delete --productid ${BUNDLE_ID}
  fi

  # Same for the package receipt; always remove it from the package receipt
  # data base.
  local readonly PKGUTIL="/usr/sbin/pkgutil"
  local readonly PACKAGE_ID_SUFFIX=".pkg"
  local readonly PACKAGE_ID="${BUNDLE_ID}${PACKAGE_ID_SUFFIX}"
  if [[ -e "${PKGUTIL}" ]]; then
    dbglog "${PKGUTIL} --forget ${PACKAGE_ID}"
    ${PKGUTIL} --forget "${PACKAGE_ID}"
  fi
}

# Delete the WebM component bundle.
delete_component "${QT_SYS_PATH}${WEBM_COMPONENT}" "${WEBM_ID}"

# Delete the XiphQT component bundle.
delete_component "${QT_SYS_PATH}${XIPHQT_COMPONENT}" "${XIPHQT_ID}"

dbglog "Done."
