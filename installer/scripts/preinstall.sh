#!/bin/bash
##
##  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.

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
    dbglog "${FUNCNAME[0]}: ${BUNDLE} does not exist."
  fi
}

dbglog "script args=$@"

webm_preinstall() {
  local readonly OLD_WEBM_COMPONENT="WebM.component"
  local readonly QT_SYS_PATH="/Library/QuickTime/"
  local readonly QT_USER_PATH="${HOME}/Library/QuickTime/"
  local readonly WEBM_COMPONENT="AWebM.component"

  # Delete any existing copy of WebM.component or AWebM.component.
  delete_component "${QT_SYS_PATH}${OLD_WEBM_COMPONENT}"
  delete_component "${QT_SYS_PATH}${WEBM_COMPONENT}"
  delete_component "${QT_USER_PATH}${OLD_WEBM_COMPONENT}"
  delete_component "${QT_USER_PATH}${WEBM_COMPONENT}"
}

xiphqt_preinstall() {
  local readonly COMPONENTS_SYS_PATH="/Library/Components/"
  local readonly COMPONENTS_USER_PATH="${HOME}/Library/Components/"
  local readonly QT_SYS_PATH="/Library/QuickTime/"
  local readonly QT_USER_PATH="${HOME}/Library/QuickTime/"
  local readonly XIPHQT_COMPONENT="XiphQT.component"

  # Delete any existing any existing copy of XiphQT.component.
  delete_component "${COMPONENTS_SYS_PATH}${XIPHQT_COMPONENT}"
  delete_component "${COMPONENTS_USER_PATH}${XIPHQT_COMPONENT}"
  delete_component "${QT_SYS_PATH}${XIPHQT_COMPONENT}"
  delete_component "${QT_USER_PATH}${XIPHQT_COMPONENT}"
}

dbglog "Done."
