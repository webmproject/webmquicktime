#!/bin/bash
##
##  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.
COMPONENTS_SYS_PATH="/Library/Components/"
COMPONENTS_USER_PATH="${HOME}/Library/Components/"
QT_SYS_PATH="/Library/QuickTime/"
QT_USER_PATH="${HOME}/Library/QuickTime/"
XIPHQT_COMPONENT="XiphQT.component"
RM="rm -r -f"

dbglog() {
  logger "XiphQT preinstall: $@"
}

dbglog "script args=$@"

# Delete any existing any existing copy of XiphQT.component.

if [[ -e "${COMPONENTS_SYS_PATH}${XIPHQT_COMPONENT}" ]]; then
  dbglog "${RM} ${COMPONENTS_SYS_PATH}${XIPHQT_COMPONENT}"
  ${RM} "${COMPONENTS_SYS_PATH}${XIPHQT_COMPONENT}"
else
  dbglog "${COMPONENTS_SYS_PATH}${XIPHQT_COMPONENT} does not exist."
fi

if [[ -e "${COMPONENTS_USER_PATH}${XIPHQT_COMPONENT}" ]]; then
  dbglog "${RM} ${COMPONENTS_USER_PATH}${XIPHQT_COMPONENT}"
  ${RM} "${COMPONENTS_USER_PATH}${XIPHQT_COMPONENT}"
else
  dbglog "${COMPONENTS_USER_PATH}${XIPHQT_COMPONENT} does not exist."
fi

if [[ -e "${QT_SYS_PATH}${XIPHQT_COMPONENT}" ]]; then
  dbglog "${RM} ${QT_SYS_PATH}${XIPHQT_COMPONENT}"
  ${RM} "${QT_SYS_PATH}${XIPHQT_COMPONENT}"
else
  dbglog "${QT_SYS_PATH}${XIPHQT_COMPONENT} does not exist."
fi

if [[ -e "${QT_USER_PATH}${XIPHQT_COMPONENT}" ]]; then
  dbglog "${RM} ${QT_USER_PATH}${XIPHQT_COMPONENT}"
  ${RM} "${QT_USER_PATH}${XIPHQT_COMPONENT}"
else
  dbglog "${QT_USER_PATH}${XIPHQT_COMPONENT} does not exist."
fi

dbglog "done."
