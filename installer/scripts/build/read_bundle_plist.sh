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

read_bundle_info() {
  local readonly BUNDLE="$1"
  local readonly NAME="$2"
  local readonly PLIST_BUDDY="/usr/local/bin/PlistBuddy"

  # Arg/tool validation.
  file_exists "${BUNDLE}" || die "${BUNDLE} does not exist."
  [[ -n "${NAME}" ]] || die "missing command name in ${FUNCNAME}."
  file_exists "${PLIST_BUDDY}" || die "${PLIST_BUDDY} does not exist."

  # When |BUNDLE| file name is a plist file, just read from the plist file.
  if [[ "$(basename "${BUNDLE}")" =~ \.plist$ ]]; then
    local readonly PLIST_PATH="${BUNDLE}"
  else
    local readonly PLIST_PATH="${BUNDLE}/Contents/Info.plist"
  fi
  file_exists "${PLIST_PATH}" || die "${PLIST_PATH} does not exist."

  # Build a command that |PLIST_BUDDY| understands, and attempt to read the
  # requested plist entry.
  local readonly COMMAND="Print :CFBundle${NAME}"
  ${PLIST_BUDDY} -c "${COMMAND}" "${PLIST_PATH}"
}

read_bundle_id() {
  read_bundle_info "$1" Identifier
}

read_bundle_version() {
  read_bundle_info "$1" Version
}

main() {
  local readonly BUNDLE="$1"
  file_exists "${BUNDLE}" || die "${BUNDLE} does not exist."

  shopt -s nocasematch
  if [[ "$2" =~ ^v$|ver|version ]]; then
    read_bundle_version "${BUNDLE}"
  elif [[ "$2" =~ ^id$|identifier ]]; then
    read_bundle_id "${BUNDLE}"
  elif [[ -z "$2" ]]; then
    echo $(read_bundle_id "${BUNDLE}") $(read_bundle_version "${BUNDLE}")
  fi
  shopt -u nocasematch
}

# Execute |main| only when not included in another script via source.
if [[ "$0" != "-bash" ]] && \
    [[ $(basename "$0") == read_bundle_plist.sh ]]; then
  main "$@"
fi
