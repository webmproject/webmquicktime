#!/bin/bash
##
##  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.
readonly KEYSTONE="/Library/Google/GoogleSoftwareUpdate/GoogleSoftwareUpdate.bundle/Contents/MacOS/ksadmin"
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
  local readonly RM="rm -r -f"
  if [[ -e "${BUNDLE}" ]]; then
    dbglog "${RM} ${BUNDLE}"
    ${RM} "${BUNDLE}"
  else
    dbglog "${BUNDLE} does not exist."
  fi
}

if [[ -e "${KEYSTONE}" ]]; then
  # Unregister the component bundles.
  dbglog "${KEYSTONE} --delete --productid ${XIPHQT_ID}"
  ${KEYSTONE} --delete --productid ${XIPHQT_ID}
  dbglog "${KEYSTONE} --delete --productid ${WEBM_ID}"
  ${KEYSTONE} --delete --productid ${WEBM_ID}
fi

# Delete the WebM component bundle.
delete_component "${QT_SYS_PATH}${WEBM_COMPONENT}"

# Delete the XiphQT component bundle.
delete_component "${QT_SYS_PATH}${XIPHQT_COMPONENT}"

dbglog "Done."
