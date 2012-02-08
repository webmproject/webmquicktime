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
readonly XIPHQT_COMPONENT="XiphQT.component"
readonly XIPHQT_ID="org.xiph.xiph-qt.xiphqt"
readonly XIPHQT_VERSION="0.1.9"
readonly UPDATE_URL="TODO"

dbglog() {
  logger "XiphQT postinstall: $@"
}

dbglog "script args=$@"

${KEYSTONE} --register --productid "${XIPHQT_ID}" --version "${VERSION}" \
  --xcpath "${QT_SYS_PATH}{XIPHQT_COMPONENT}" --url "${UPDATE_URL}"
