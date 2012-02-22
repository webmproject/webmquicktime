#!/bin/bash
##
##  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.

errorlog() {
  echo "$(basename "$0"): [31;40m$@[0m"
}

warninglog() {
  echo "$(basename "$0"): [33;40m$@[0m"
}

debuglog() {
  echo "$(basename "$0"): [32;40m$@[0m"
}

infolog() {
  echo "$(basename "$0"): $@"
}

die() {
  errorlog "ERROR, $@"
  exit 1
}

copy_bundle() {
  local readonly BUNDLE="$1"
  local readonly TARGET="$2"

  if [[ -z "${BUNDLE}" ]] || [[ ! -e "${BUNDLE}" ]]; then
    die "ERROR, invalid bundle path passed to copy_bundle, quitting." \
        "bundle path=${BUNDLE}"
  fi

  if [[ -z "${TARGET}" ]]; then
    die "ERROR, invalid target path passed to copy_bundle, quitting." \
        "target path=${TARGET}"
  fi

  local readonly COPY="cp -p -R"
  ${COPY} "${BUNDLE}" "${TARGET}"
}

file_exists() {
  return $(test -n "$1" && test -e "$1")
}

