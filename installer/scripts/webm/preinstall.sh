#!/bin/bash
##
##  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.
OLD_WEBM_COMPONENT="WebM.component"
QT_SYS_PATH="/Library/QuickTime/"
QT_USER_PATH="${HOME}/Library/QuickTime/"
WEBM_COMPONENT="AWebM.component"
RM="rm -r -f"

dbglog() {
  logger "WebM preinstall: $@"
}

dbglog "script args=$@"

# Delete any existing copy of WebM.component or AWebM.component.

if [[ -e "${QT_SYS_PATH}${OLD_WEBM_COMPONENT}" ]]; then
  dbglog "${RM} ${QT_SYS_PATH}${OLD_WEBM_COMPONENT}"
  ${RM} "${QT_SYS_PATH}${OLD_WEBM_COMPONENT}"
else
  dbglog "${QT_SYS_PATH}${OLD_WEBM_COMPONENT} does not exist."
fi

if [[ -e "${QT_SYS_PATH}${WEBM_COMPONENT}" ]]; then
  dbglog "${RM} ${QT_SYS_PATH}${WEBM_COMPONENT}"
  ${RM} "${QT_SYS_PATH}${WEBM_COMPONENT}"
else
  dbglog "${QT_SYS_PATH}${WEBM_COMPONENT} does not exist."
fi

if [[ -e "${QT_USER_PATH}${OLD_WEBM_COMPONENT}" ]]; then
  dbglog "${RM} ${QT_USER_PATH}${OLD_WEBM_COMPONENT}"
  ${RM} "${QT_USER_PATH}${OLD_WEBM_COMPONENT}"
else
  dbglog "${QT_USER_PATH}${OLD_WEBM_COMPONENT} does not exist."
fi

if [[ -e "${QT_USER_PATH}${WEBM_COMPONENT}" ]]; then
  dbglog "${RM} ${QT_USER_PATH}${WEBM_COMPONENT}"
  ${RM} "${QT_USER_PATH}${WEBM_COMPONENT}"
else
  dbglog "${QT_USER_PATH}${WEBM_COMPONENT} does not exist."
fi

