#!/bin/bash
##
##  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.
KEYSTONE="/Library/Google/GoogleSoftwareUpdate/GoogleSoftwareUpdate.bundle/Contents/MacOS/ksadmin"
QT_SYS_PATH="/Library/QuickTime/"
RM="rm -r -f"
WEBM_COMPONENT="AWebM.component"
WEBM_ID="org.webmproject.webmquicktime.component"
XIPHQT_COMPONENT="XiphQT.component"
XIPHQT_ID="org.xiph.xiph-qt.xiphqt"

dbglog() {
  logger "WebM QuickTime uninstall_helper: $@"
}

if [[ -e "${KEYSTONE}" ]]; then
  # Unregister the component bundles.
  dbglog "${KEYSTONE} --delete --productid ${XIPHQT_ID}"
  ${KEYSTONE} --delete --productid ${XIPHQT_ID}
  dbglog "${KEYSTONE} --delete --productid ${WEBM_ID}"
  ${KEYSTONE} --delete --productid ${WEBM_ID}
fi

# Delete the WebM component bundle.
if [[ -e "${QT_SYS_PATH}${WEBM_COMPONENT}" ]]; then
  dbglog "${RM} ${QT_SYS_PATH}${WEBM_COMPONENT}"
  ${RM} "${QT_SYS_PATH}${WEBM_COMPONENT}"
else
  dbglog "${QT_SYS_PATH}${WEBM_COMPONENT} does not exist."
fi

# Delete the XiphQT component bundle.
if [[ -e "${QT_SYS_PATH}${XIPHQT_COMPONENT}" ]]; then
  dbglog "${RM} ${QT_SYS_PATH}${XIPHQT_COMPONENT}"
  ${RM} "${QT_SYS_PATH}${XIPHQT_COMPONENT}"
else
  dbglog "${QT_SYS_PATH}${XIPHQT_COMPONENT} does not exist."
fi

dbglog "Done."