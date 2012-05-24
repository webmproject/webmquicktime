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
readonly WEBM_VERSION="0.3.0"
readonly UPDATE_URL="https://tools.google.com/service/update2"

dbglog() {
  logger "WebM postinstall: $@"
}

dbglog "script args=$@"

${KEYSTONE} --register \
  --productid "${WEBM_ID}" \
  --version "${WEBM_VERSION}" \
  --xcpath "${QT_SYS_PATH}${WEBM_COMPONENT}" \
  --url "${UPDATE_URL}"
