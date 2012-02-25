// Copyright (c) 2012 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#include "keystone_util.h"

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "bundle_info.h"

// Returns path to user home directory. Checks both environment and password
// database; password database value takes precendence when it differs from
// value returned by |getenv|. Path returned always ends with '/'.
std::string ReadHomeDirectoryPath() {
  // Read HOME value from environment.
  const char* ptr_env_home_dir = getenv("HOME");
  std::string home_dir = ptr_env_home_dir;

  // Check with password database for home directory. No guarantee that HOME
  // environment variable is set or valid.

  // Obtain max size for |passwd| struct from |sysconf|.
  struct passwd passwd_entry = {0};
  const size_t passwd_buf_size = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (passwd_buf_size > 0) {
    char* const passwd_buf = new (std::nothrow) char[passwd_buf_size];
    if (passwd_buf) {
      struct passwd* ptr_passwd_entry = NULL;
      const int result = getpwuid_r(getuid(),
                                    &passwd_entry,
                                    passwd_buf,
                                    passwd_buf_size,
                                    &ptr_passwd_entry);
      if (!result && ptr_passwd_entry == &passwd_entry) {
        const std::string passwd_home_dir = passwd_entry.pw_dir;
        if (home_dir != passwd_home_dir) {
          home_dir = passwd_home_dir;
        }
      }
      delete[] passwd_buf;
    }
  }
  if (!home_dir.empty() && home_dir[home_dir.size()] != '/') {
    home_dir.append("/");
  }
  return home_dir;
}

// Returns true when |path| exists.
bool PathExists(const std::string& path) {
  struct stat path_stat = {0};
  const int status = stat(path.c_str(), &path_stat);
  return status == 0;
}

// Returns true when |path| exists and is a directory.
bool PathIsDirectory(const std::string& path) {
  if (PathExists(path)) {
    struct stat path_stat = {0};
    const int status = stat(path.c_str(), &path_stat);
    if (status == 0 && S_ISDIR(path_stat.st_mode)) {
      return true;
    }
  }
  return false;
}

// Appends activity directory to string returned by |ReadHomeDirectoryPath()|
// and returns result.
std::string GenerateActivityDirectoryPath() {
  const std::string home_dir = ReadHomeDirectoryPath();
  if (home_dir.empty() || !PathIsDirectory(home_dir)) {
    // Return an empty string: can't really do anything without a valid
    // |home_dir|.
    return std::string();
  }

  const std::string kActivityDirectory =
      "Library/Google/GoogleSoftwareUpdate/Actives/";
  return home_dir + kActivityDirectory;
}

// Creates directory specified by |path|. Creates parent directories if
// necessary. Returns 0 when successful.
int CreateDirectory(const std::string& path) {
  if (path.empty()) {
    return -1;
  }

  // Search |path| for /'s, and create all directories encountered.
  // Start at |pos| 1: assume filesystem root exists.
  typedef std::string::size_type size_type;
  using std::string;
  for (size_type pos = 1; pos < path.length() && pos != string::npos; ++pos) {
    pos = path.find('/', pos);
    if (pos != std::string::npos) {
      // Copy from start through occurence of '/';
      const std::string directory = path.substr(0, pos);

      if (!PathExists(directory)) {
        // Make the directory with permissions allowing r/w for everyone.
        const mode_t mode_flags =
            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
        if (mkdir(directory.c_str(), mode_flags)) {
          // Could not create directory.
          return -1;
        }
      }
    }
  }
  return 0;
}

void TouchActivityFile() {
  // Generate the activity directory path for the current user, and create it
  // if it does not exist.
  const std::string activity_dir = GenerateActivityDirectoryPath();
  if (!PathIsDirectory(activity_dir)) {
    if (CreateDirectory(activity_dir)) {
      // Unable to create directory, abandon touch attempt.
      return;
    }
  }

  const std::string activity_file = activity_dir + kWebmBundleId;
  if (PathExists(activity_file)) {
    // There's nothing to do when the file exists. Keystone deletes it each
    // time it runs the activity check.
    return;
  }

  // Create |activity_file|.
  const int open_flags = O_WRONLY | O_CREAT | O_NONBLOCK | O_NOCTTY;
  const mode_t mode_flags =
      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  int file_desc = open (activity_file.c_str(), open_flags, mode_flags);
  if (file_desc == -1) {
    // |open failed|, give up.
    return;
  }
  close(file_desc);
}
