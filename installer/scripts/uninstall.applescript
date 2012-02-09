(*
Copyright (c) 2012 The WebM project authors. All Rights Reserved.
Use of this source code is governed by a BSD-style license
that can be found in the LICENSE file in the root of the source
tree. An additional intellectual property rights grant can be found
in the file PATENTS.  All contributing project authors may
be found in the AUTHORS file in the root of the source tree.
*)


tell application "Finder" to get POSIX path of ((container of (path to me)) as text)
do shell script quoted form of result & "./uninstall_helper.sh" with administrator privileges
