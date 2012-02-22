#!/bin/bash
##
##  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.
set -e

if [[ $(basename $(pwd)) != "installer" ]] || \
    [[ $(basename $(dirname $(pwd))) != "webmquicktime" ]]; then
  echo "$(basename $0) must be run from webmquicktime/installer"
  exit 1
fi

source scripts/build/util.sh

readonly BUNDLE="$1"
readonly PLIST_BUDDY="/usr/local/bin/PlistBuddy"

read_bundle_info() {
  local readonly NAME="$1"
  local readonly COMMAND="Print :CFBundle${NAME}"
  local readonly PLIST_PATH="${BUNDLE}/Contents/Info.plist"
  file_exists "${PLIST_PATH}" || die "${PLIST_PATH} does not exist."
  echo $(${PLIST_BUDDY} -c "${COMMAND}" "${PLIST_PATH}")
}

read_bundle_id() {
  echo $(read_bundle_info Identifier)
}

read_bundle_version() {
  echo $(read_bundle_info Version)
}

file_exists "${PLIST_BUDDY}" || die "${PLIST_BUDDY} does not exist."
file_exists "${BUNDLE}" || die "${BUNDLE} does not exist."

shopt -s nocasematch
if [[ "$2" =~ ^v$|ver|version ]]; then
  read_bundle_version
elif [[ "$2" =~ ^id$|identifier ]]; then
  read_bundle_id
elif [[ -z "$2" ]]; then
  echo $(read_bundle_id) $(read_bundle_version)
fi
shopt -u nocasematch
