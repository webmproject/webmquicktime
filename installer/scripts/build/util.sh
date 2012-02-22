#!/bin/bash
##
##  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.

# Outputs arguments in bold red text using echo.
errorlog() {
  echo -e "$(basename "$0"): \x1b[1;31;01m$@\x1b[0m"
}

# Outputs arguments in bold yellow text using echo.
warninglog() {
  echo -e "$(basename "$0"): \x1b[1;33;01m$@\x1b[0m"
}

# Outputs arguments in bold green text using echo.
debuglog() {
  echo -e "$(basename "$0"): \x1b[1;32;01m$@\x1b[0m"
}

# Outputs arguments in bold text using echo.
infolog() {
  echo -e "$(basename "$0"): \x1b[1m$@\x1b[0m"
}

# Outputs arguments in blinking bold red text, then exit's with failure code.
die() {
  echo -e "$(basename "$0"): \x1b[5;31;01mERROR: $@\x1b[0m"
  exit 1
}

# Copies a bundle while preserving permissions.
copy_bundle() {
  local readonly BUNDLE="$1"
  local readonly TARGET="$2"

  file_exists "${BUNDLE}" || \
      die "invalid bundle path (${BUNDLE}) passed to copy_bundle, quitting."

  if [[ -z "${TARGET}" ]]; then
    die "invalid target path (${TARGET}) passed to copy_bundle, quitting."
  fi

  local readonly COPY="cp -p -R"
  ${COPY} "${BUNDLE}" "${TARGET}"
}

file_exists() {
  test -n "$1" && test -e "$1"
}

