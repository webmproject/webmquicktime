#!/bin/bash
##
##  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.

readonly OSACOMPILE="/usr/bin/osacompile -o"
readonly UNINSTALLER_BUNDLE="uninstall.app"
readonly UNINSTALL_SCRIPT="scripts/uninstall.applescript"

dbglog() {
  echo "build_uninstaller: $@"
}

dbglog "${OSACOMPILE} ${UNINSTALLER_BUNDLE} ${UNINSTALL_SCRIPT}"
${OSACOMPILE} "${UNINSTALLER_BUNDLE}" "${UNINSTALL_SCRIPT}"
dbglog "Done."
