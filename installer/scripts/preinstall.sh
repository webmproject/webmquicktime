#!/bin/bash
##
##  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.
readonly OLD_WEBM_COMPONENT="WebM.component"
readonly QT_SYS_PATH="/Library/QuickTime/"
readonly QT_USER_PATH="${HOME}/Library/QuickTime/"
readonly WEBM_COMPONENT="AWebM.component"

dbglog() {
  logger "WebM preinstall: $@"
}

delete_component() {
  local readonly BUNDLE="$1"
  local readonly RM="rm -r -f"
  if [[ -e "${BUNDLE}" ]]; then
    dbglog "${RM} ${BUNDLE}"
    ${RM} "${BUNDLE}"
  else
    dbglog "${BUNDLE} does not exist."
  fi
}

dbglog "script args=$@"

# Delete any existing copy of WebM.component or AWebM.component.
delete_component "${QT_SYS_PATH}${OLD_WEBM_COMPONENT}"
delete_component "${QT_SYS_PATH}${WEBM_COMPONENT}"
delete_component "${QT_USER_PATH}${OLD_WEBM_COMPONENT}"
delete_component "${QT_USER_PATH}${WEBM_COMPONENT}"

dbglog "Done."
