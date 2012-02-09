#!/usr/bin/python
# Copyright 2008 Google Inc.  All rights reserved.

"""This script will install Keystone in the correct context
(system-wide or per-user).  It can also uninstall Keystone.  is run by
KeystoneRegistration.framework.

Example command lines for testing:
Install:    install.py --install=/tmp/Keystone.tbz --root=/Users/fred
Uninstall:  install.py --nuke --root=/Users/fred

Example real command lines, for user and root install and uninstall:
  install.py --install Keystone.tbz
  install.py --nuke
  sudo install.py --install Keystone.tbz
  sudo install.py --nuke

For a system-wide Keystone, the install root is "/".  Run with --help
for a list of options.  Use --no-launchdjobs to NOT start background
processes.

Errors can happen if:
 - we don't have write permission to install in the given root
 - pieces of our install are missing

On error, we print an message on stdout and our exit status is
non-zero.  On success, we print nothing and exit with a status of 0.
"""

import os
import re
import sys
import pwd
import stat
import glob
import getopt
import shutil
import platform
import fcntl


# Allow us to force the installer to think we're on Tiger (10.4)
FORCE_TIGER = False

# Allow us to adjust the agent launch interval (for testing).
# In seconds.  Time is 1 hour minus a jitter factor.
AGENT_START_INTERVAL = 3523

# Name of our "lockdown" ticket.  If you change this name be sure to
# change it in other places in the code (grep is your friend)
LOCKDOWN_TICKET = 'com.google.Keystone.Lockdown'

# Process that we consider a marker of a running user login session
USER_SESSION_PROCESSNAME = \
    ' /System/Library/CoreServices/Finder.app/Contents/MacOS/Finder'


class Error(Exception):
  """Generic exception for Keystone install failure."""

  def __init__(self, package, root, msg):
    self.package = package
    self.root = root
    self.msg = msg

  def __str__(self):
    return 'Package: %s, Root: %s, Error: %s' % (self.package, self.root,
                                                 self.msg)


def CheckOnePath(file, statmode):
  """Sanity check a file or directory as requested.  On failure throw
  an exception."""
  if os.path.exists(file):
    st = os.stat(file)
    if (st.st_mode & statmode) != 0:
      return
  raise Error('None', 'None', 'Path check failure for "%s" mode %s' %
              (file, statmode))


# -------------------------------------------------------------------------

class KeystoneInstall(object):
  """Worker object which does the heavy lifting of install or uninstall.
  By default it assumes 10.5 (Leopard).

  Args:
    package: The package to install (i.e. Keystone.tbz)
    is_system: True if this is a system Keystone install
    agent_job_uid: uid to start agent jobs as or None to use current euid
    root: root directory for install.  On System this would be "/";
          else would be a user home directory (unless testing, in which case
          the root can be anywhere).
    launchd_setup: True if the installation should setup launchd job description
                   plists (and Tiger equivalents)
    launchd_jobs: True if the installation should start/stop related jobs
    self_destruct: True if uninstall is being triggered by a process the
                   uninstall is expected to kill

  Conventions:
    All functions which return directory paths end in '/'
  """

  def __init__(self, package, is_system, agent_job_uid, root,
               launchd_setup, launchd_jobs, self_destruct):
    self.package = package
    self.is_system = is_system
    self.agent_job_uid = agent_job_uid
    if is_system:
      assert agent_job_uid is not None, 'System install needs agent job uid'
    self.root = root
    if not self.root.endswith('/'):
      self.root = self.root + '/'
    self.launchd_setup = launchd_setup
    self.launchd_jobs = launchd_jobs
    self.self_destruct = self_destruct
    self.cached_package_version = None
    # Save/restore permissions
    self.old_euid = None
    self.old_egid = None
    self.old_umask = None

  def RunCommand(self, cmd):
    """Runs a command, returning return code and output.

    Returns:
      Tuple of return value, stdout and stderr.
    """
    # We need to work in python 2.3 (OSX 10.4), 2.5 (10.5), and 2.6 (10.6)
    if (sys.version_info[0] == 2) and (sys.version_info[1] <= 5):
      # subprocess.communicate implemented the hard way
      import errno
      import popen2
      import select
      p = popen2.Popen3(cmd, True)
      stdout = []
      stderr = []
      readable = [ p.fromchild, p.childerr ]
      while not p.fromchild.closed or not p.childerr.closed:
        try:
          try_to_read = []
          if not p.fromchild.closed:
            try_to_read.append(p.fromchild)
          if not p.childerr.closed:
            try_to_read.append(p.childerr)
          readable, ignored_w, ignored_x = select.select(try_to_read, [], [])
        except select.error, e:
          if e.args[0] == errno.EINTR:
            continue
          raise
        if p.fromchild in readable:
          out = os.read(p.fromchild.fileno(), 1024)
          stdout.append(out)
          if out == '':
            p.fromchild.close()
        if p.childerr in readable:
          errout = os.read(p.childerr.fileno(), 1024)
          stderr.append(errout)
          if errout == '':
            p.childerr.close()
      result = p.wait()
      return (os.WEXITSTATUS(result), ''.join(stdout), ''.join(stderr))
    else:
      # Just use subprocess, so much simpler
      import subprocess
      p = subprocess.Popen(cmd, shell=False, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, close_fds=True)
      (stdout, stderr) = p.communicate()
      return (p.returncode, stdout, stderr)

  def _AgentProcessName(self):
    """Return the process name of the agent."""
    return 'GoogleSoftwareUpdateAgent'

  def _LibraryCachesDirPath(self):
    """Return the Library/Caches subdirectory"""
    return os.path.join(self.root, 'Library/Caches/')

  def _LibraryGoogleDirPath(self):
    """Return the Library subdirectory that parents all our dirs"""
    return os.path.join(self.root, 'Library/Google/')

  def _KeystoneDirPath(self):
    """Return the subdirectory where Keystone.bundle is or will be.
    Does not sanity check the directory."""
    return os.path.join(self._LibraryGoogleDirPath(), 'GoogleSoftwareUpdate/')

  def _KeystoneBundlePath(self):
    """Return the location of Keystone.bundle."""
    return os.path.join(self._KeystoneDirPath(), 'GoogleSoftwareUpdate.bundle/')

  def _KeystoneTicketStorePath(self):
    """Returns directory path of the Keystone ticket store."""
    return os.path.join(self._KeystoneDirPath(), 'TicketStore')

  def _KsadminPath(self):
    """Return a path to ksadmin which will exist only AFTER Keystone is
    installed.  Return None if it doesn't exist."""
    ksadmin = os.path.join(self._KeystoneBundlePath(), 'Contents/MacOS/ksadmin')
    if not os.path.exists(ksadmin):
      return None
    return ksadmin

  def _KeystoneResourcePath(self):
    """Return the subdirectory where Keystone.bundle's resources should be."""
    return os.path.join(self._KeystoneBundlePath(), 'Contents/Resources/')

  def _KeystoneAgentPath(self):
    """Returns a path to installed KeystoneAgent.app."""
    return os.path.join(self._KeystoneResourcePath(),
                        'GoogleSoftwareUpdateAgent.app/')

  def _LaunchAgentConfigDir(self):
    """Return the destination directory where launch agents should go."""
    return os.path.join(self.root, 'Library/LaunchAgents/')

  def _LaunchDaemonConfigDir(self):
    """Return the destination directory where launch daemons should go."""
    return os.path.join(self.root, 'Library/LaunchDaemons/')

  def _KeystoneTicketURL(self):
    """Return the URL for Keystone's ticket, possibly from a defaults file."""
    # Rather than spam the console for this a lot when missing, existence
    # check first.
    cmd = ['/usr/bin/defaults', 'domains' ]
    (result, out, errout) = self.RunCommand(cmd)
    # Rough check only
    if out.find('com.google.KeystoneInstall') != -1:
      cmd = ['/usr/bin/defaults', 'read', 'com.google.KeystoneInstall', 'URL']
      (result, out, errout) = self.RunCommand(cmd)
      url = out.strip()
      if result == 0 and len(url) > 0:
        return url
    return 'https://tools.google.com/service/update2'

  def _AgentPlistFileName(self):
    """Return the filename of the Keystone agent launchd plist or None."""
    return 'com.google.keystone.agent.plist'

  def _DaemonPlistFileName(self):
    """Return the filename of the Keystone daemon launchd plist."""
    return 'com.google.keystone.daemon.plist'

  def InstalledKeystoneBundleVersion(self):
    """Return the version of an installed Keystone bundle, or None if
    not installed.  Specifically, it returns the CFBundleVersion as a
    string (e.g. "0.1.0.0").

    Invariant: we require a 4-digit version when building Keystone.bundle.
    """
    defaults_domain = os.path.join(self._KeystoneBundlePath(), 'Contents/Info')
    if not os.path.exists(defaults_domain + '.plist'):
      return None
    cmd = ['/usr/bin/defaults', 'read', defaults_domain, 'CFBundleVersion']
    (result, out, errout) = self.RunCommand(cmd)
    if result != 0:
      raise Error(self.package, self.root,
                  'Unable to read installed CFBundleVersion: "%s"' % errout)
    return out.strip()

  def MyKeystoneBundleVersion(self):
    """Return the version of our Keystone bundle which we might want to install.
    Specifically, it returns the CFBundleVersion as a string (e.g. "0.1.0.0").

    Invariant: we require a 4-digit version when building Keystone.bundle.
    """
    if self.cached_package_version is None:
      cmd = ['/usr/bin/tar', '-Oxjf',
             self.package,
             'GoogleSoftwareUpdate.bundle/Contents/Info.plist']
      (result, out, errout) = self.RunCommand(cmd)
      if result != 0:
        raise Error(self.package, self.root,
                    'Unable to read package Info.plist: "%s"' % errout)
      # walking by index instead of implicit iterator so we can easily
      # "get next"
      linelist = out.splitlines()
      for i in range(len(linelist)):
        if linelist[i].find('<key>CFBundleVersion</key>') != -1:
          version = linelist[i+1].strip()
          version = version.strip('<string>').strip('</string>')
          self.cached_package_version = version
          break
    return self.cached_package_version

  def IsVersionGreaterThanVersion(self, a_version, b_version):
    """Return True if a_version is greater than b_version.

    Invariant: we require a 4-digit version when building Keystone.bundle.
    """
    if a_version is None or b_version is None:
      return True
    else:
      a_version = a_version.split('.')
      b_version = b_version.split('.')
    # Only correct for 4-digit versions, see invariants.
    if len(a_version) != len(b_version):
      return True
    for a, b in zip(a_version, b_version):
      if int(a) > int(b):
        return True
      elif int(a) < int(b):
        return False
    # If we get here, it's a complete match, so no.
    return False

  def IsMyVersionGreaterThanInstalledVersion(self):
    """Returns True if package Keystone version is greater than current install.

    Invariant: we require a 4-digit version when building Keystone.bundle.
    """
    my_version = self.MyKeystoneBundleVersion()
    installed_version = self.InstalledKeystoneBundleVersion()
    return self.IsVersionGreaterThanVersion(my_version, installed_version)

  def _SetSystemInstallPermissions(self):
    """Set permissions for system install, must pair with
    _ClearSystemInstallPermissions(). Call before any filesystem access."""
    assert (self.old_euid is None and self.old_egid is None and
            self.old_umask is None), 'System permissions used reentrant'
    self.old_euid = os.geteuid()
    os.seteuid(0)
    self.old_egid = os.getegid()
    os.setegid(0)
    self.old_umask = os.umask(022)

  def _ClearSystemInstallPermissions(self):
    """Restore prior permissions after _SetSystemInstallPermissions()."""
    assert (self.old_euid is not None and self.old_egid is not None and
            self.old_umask is not None), 'System permissions cleared before set'
    os.seteuid(self.old_euid)
    self.old_euid = None
    os.setegid(self.old_egid)
    self.old_egid = None
    os.umask(self.old_umask)
    self.old_umask = None

  def _InstallPlist(self, plist, dest_dir):
    """Install a copy of the plist from Resources to the dest_dir path.
    For system install, assumes you have already called
    _SetSystemInstallPermissions().
    """
    try:
      pf = open(os.path.join(self._KeystoneResourcePath(), plist), 'r')
      content = pf.read()
      pf.close()
    except IOError, e:
      raise Error(self.package, self.root,
                  'Failed to read resource launchd plist "%s": %s' %
                  (plist, str(e)))
    # This line is key.  We can't have a tilde in a launchd script;
    # we need an absolute path.  So we replace a known token, like this:
    #    cat src.plist | 's/INSTALL_ROOT/self.root/g' > dest.plist
    content = content.replace('${INSTALL_ROOT}', self.root)
    content = content.replace(self.root + '/', self.root)  # doubleslash remove
    # Make sure launchd can distinguish between user and system Agents.
    # This is a no-op for the daemon.
    if self.is_system:
      content = content.replace('${INSTALL_TYPE}', 'root')
    else:
      content = content.replace('${INSTALL_TYPE}', 'user')
    # Allow start interval to be configured.
    content = content.replace('${START_INTERVAL}', str(AGENT_START_INTERVAL))
    try:
      # Write to temp file then move in place (safe save)
      target_file = os.path.join(dest_dir, plist)
      target_tmp_file = target_file + '.tmp'
      pf = open(target_tmp_file, 'w')
      pf.write(content)
      pf.close()
      os.rename(target_tmp_file, target_file)
    except IOError, e:
      raise Error(self.package, self.root,
                  'Failed to install launchd plist "%s": %s' %
                  (os.path.join(dest_dir, plist), str(e)))

  def _InstallAgentLoginItem(self):
    """Setup the agent login item (vs. launchd job).
    Assumes _SetSystemInstallPermissions() has been called."""
    pass

  def _RemoveAgentLoginItem(self):
    """Remove the agent login item (vs. launchd job).
    Assumes _SetSystemInstallPermissions() has been called.

    Note: We use this code on both Tiger and Leopard to handle the OS upgrade
    case.
    """
    if self.is_system:
      domain = '/Library/Preferences/loginwindow'
    else:
      domain = 'loginwindow'
    (result, alaout, errout) = self.RunCommand(['/usr/bin/defaults', 'read',
        domain, 'AutoLaunchedApplicationDictionary'])
    # Ignoring result
    if len(alaout.strip()) == 0:
      alaout = '()'
    # One line per loginitem to help us match
    alaout = re.compile('[\n]+').sub('', alaout)
    # handles case where we are the only item
    alaout = alaout.replace('(', '(\n')
    alaout = alaout.replace('}', '}\n')
    needed_removal = False
    for line in alaout.splitlines():
      if line.find('/Library/Google/GoogleSoftwareUpdate/'
                   'GoogleSoftwareUpdate.bundle/Contents/'
                   'Resources/GoogleSoftwareUpdateAgent.app') != -1:
        alaout = alaout.replace(line, '')
        needed_removal = True
    alaout = alaout.replace('\n', '')
    # make sure it's a well-formed list
    alaout = alaout.replace('(,', '(')
    if needed_removal:
      (result, out, errout) = self.RunCommand(['/usr/bin/defaults', 'write',
          domain, 'AutoLaunchedApplicationDictionary', alaout])
      # Ignore result, if we messed up the parse just move on.

  def _ChangeDaemonRunStatus(self, start, ignore_failure):
    """Start or stop the daemon using launchd."""
    assert self.is_system, 'Daemon start on non-system install'
    self._SetSystemInstallPermissions()
    try:
      if start:
        action = 'load'
      else:
        action = 'unload'
      (result, out, errout) = self.RunCommand(['/bin/launchctl', action,
          os.path.join(self._LaunchDaemonConfigDir(),
                       self._DaemonPlistFileName())])
      if not ignore_failure and result != 0:
        raise Error(self.package, self.root, 'Failed to %s daemon (%d): %s' %
                    (action, result, errout))
    finally:
      self._ClearSystemInstallPermissions()

  def _ChangeAgentRunStatus(self, start, ignore_failure):
    """Start or stop the agent using launchd."""
    if self._AgentPlistFileName() is None:
      return
    if start:
      action = 'load'
      search_process_name = USER_SESSION_PROCESSNAME
    else:
      action = 'unload'
      search_process_name = self._AgentProcessName()
    if self.is_system:
      self._SetSystemInstallPermissions()
      try:
        # System installation needs to use bsexec to hit all the running agents
        (result, psout, pserr) = self.RunCommand(['/bin/ps', 'auxwww'])
        if result != 0:  # Internal problem so don't use ignore_failure
          raise Error(self.package, self.root,
                      'Could not run /bin/ps: %s' % pserr)
        for psline in psout.splitlines():
          if psline.find(search_process_name) != -1:
            username = psline.split()[0]
            uid = pwd.getpwnam(username)[2]
            # Must be root to bsexec.
            # Must bsexec to (pid) to get in local user's context.
            # Must become local user to have right process owner.
            # Must unset SUDO_COMMAND to keep launchctl happy.
            # Order is important.
            agent_plist_path = os.path.join(self._LaunchAgentConfigDir(),
                                            self._AgentPlistFileName())
            (result, out, errout) = self.RunCommand([
                '/bin/launchctl', 'bsexec', psline.split()[1],
                '/usr/bin/sudo', '-u', username, '/bin/bash', '-c',
                'unset SUDO_COMMAND ; /bin/launchctl %s -S Aqua "%s"' % (
                    action,
                    os.path.join(self._LaunchAgentConfigDir(),
                                 self._AgentPlistFileName()))])
            # Although we're running for every user, only treat the requested
            # user as an error
            if not ignore_failure and result != 0 and uid == self.agent_job_uid:
              raise Error(self.package, self.root,
                  'Failed to %s agent for uid %d from plist "%s" (%d): %s' %
                  (action, self.agent_job_uid, agent_plist_path, result,
                   errout))
      finally:
        self._ClearSystemInstallPermissions()
    else:
      # Non-system variant requires basic launchctl commands
      agent_plist_path = os.path.join(self._LaunchAgentConfigDir(),
                                      self._AgentPlistFileName())
      (result, out, errout) = self.RunCommand(['/bin/launchctl', action,
                                               '-S', 'Aqua', agent_plist_path])
      if not ignore_failure and result != 0:
        raise Error(self.package, self.root,
                    'Failed to %s agent from plist "%s" (%d): %s' %
                    (action, agent_plist_path, result, errout))

  def _ClearQuarantine(self, path):
    """Remove LaunchServices quarantine attributes from a file hierarchy."""
    # /usr/bin/xattr* are implemented in Python, and there's much magic
    # around which of /usr/bin/xattr and the multiple /usr/bin/xattr-2.?
    # actually execute. I suspect at least some users have /usr/bin/python
    # linked to a "real" copy or otherwise replaced, so we're going to
    # try a bunch of different options.
    # Implement it ourself
    try:
      import xattr
      for (root, dirs, files) in os.walk(path):
        for name in files:
          attrs = xattr.xattr(os.path.join(path, name))
          try:
            del attrs['com.apple.quarantine']
          except KeyError:
            pass
      return  # Success
    except:
      pass
    # Use specific version by name in case /usr/bin/python isn't the Apple magic
    # that selects the right copy of xattr. xattr-2.6 present on 10.6 and 10.7.
    if os.path.exists('/usr/bin/xattr-2.6'):
      (result, out, errout) = self.RunCommand(['/usr/bin/xattr-2.6', '-dr',
                                               'com.apple.quarantine', path])
      if result == 0:
        return
    # Fall back to /usr/bin/xattr. On Leopard it doesn't support '-r' so
    # recurse using find. Ignore the result, this is our last attempt.
    self.RunCommand(['/usr/bin/find', '-x', path, '-exec', '/usr/bin/xattr',
                     '-d', 'com.apple.quarantine', '{}'])

  def Install(self):
    """Perform a complete install operation, including safe upgrade"""
    # Unload any current processes but ignore failures
    if self.launchd_setup and self.launchd_jobs:
      self._ChangeAgentRunStatus(False, True)
      if self.is_system:
        self._ChangeDaemonRunStatus(False, True)
    # Install new files
    if self.is_system:
      self._SetSystemInstallPermissions()
    try:
      # Make and protect base directories (always safe during upgrade)
      if not os.path.isdir(self._KeystoneDirPath()):
        os.makedirs(self._KeystoneDirPath())
      if self.is_system:
        os.chown(self._KeystoneDirPath(), 0, 0)
        os.chmod(self._KeystoneDirPath(), 0755)
        os.chown(self._LibraryGoogleDirPath(), 0, 0)
        os.chmod(self._LibraryGoogleDirPath(), 0755)
      # Unpack Keystone bundle. In an upgrade we want to try to restore
      # to the old binary if install encounters a problem. Options flag names
      # chosen to be compatible with both 10.4 and 10.6 (both BSD tar, but
      # very different versions).
      saved_bundle_path = self._KeystoneBundlePath().rstrip('/') + '.old'
      if os.path.exists(self._KeystoneBundlePath()):
        if os.path.isdir(saved_bundle_path):
          shutil.rmtree(saved_bundle_path)
        elif os.path.exists(saved_bundle_path):
          os.unlink(saved_bundle_path)
        os.rename(self._KeystoneBundlePath(), saved_bundle_path)
      cmd = ['/usr/bin/tar', 'xjf', self.package, '--no-same-owner',
             '-C', self._KeystoneDirPath()]
      (result, out, errout) = self.RunCommand(cmd)
      if result != 0:
        try:
          if os.path.exists(saved_bundle_path):
            os.rename(saved_bundle_path, self._KeystoneBundlePath())
        finally:
          raise Error(self.package, self.root,
                      'Unable to unpack package: "%s"' % errout)
      if os.path.exists(saved_bundle_path):
        shutil.rmtree(saved_bundle_path)
      # Clear quarantine on the new bundle. Failure is ignored, user will
      # be prompted if quarantine is not cleared, but we will still operate
      # correctly.
      self._ClearQuarantine(self._KeystoneBundlePath())
      # Create Keystone ticket store. On a system install start by checking
      # ticket store permissions. Bad permissions on the store could be the
      # result of a prior install or an attempt to poison the store.
      if self.is_system and os.path.exists(self._KeystoneTicketStorePath()):
        s = os.lstat(self._KeystoneTicketStorePath())
        if (s[stat.ST_UID] == 0 and
            (s[stat.ST_GID] == 0 or s[stat.ST_GID] == 80)):
          pass
        else:
          if os.path.isdir(self._KeystoneTicketStorePath()):
            shutil.rmtree(self._KeystoneTicketStorePath())
          else:
            os.unlink(self._KeystoneTicketStorePath())
      # Now create and protect ticket store
      if not os.path.isdir(self._KeystoneTicketStorePath()):
        os.makedirs(self._KeystoneTicketStorePath())
      if self.is_system:
        os.chown(self._KeystoneTicketStorePath(), 0, 0)
        os.chmod(self._KeystoneTicketStorePath(), 0755)
      # Create/update Keystone ticket
      ksadmin_path = self._KsadminPath()
      if not ksadmin_path or not os.path.exists(ksadmin_path):
        raise Error(self.package, self.root, 'ksadmin not available')
      cmd = [ksadmin_path,
             # store is specified explicitly so unit tests work
             '--store', os.path.join(self._KeystoneTicketStorePath(),
                                     'Keystone.ticketstore'),
             '--register',
             '--productid', 'com.google.Keystone',
             '--version', self.InstalledKeystoneBundleVersion(),
             '--xcpath', ksadmin_path,
             '--url', self._KeystoneTicketURL(),
             '--preserve-tttoken']
      (result, out, errout) = self.RunCommand(cmd)
      if result != 0:
        raise Error(self.package, self.root,
            'Keystone ticket install failed (%d): %s' % (result, errout))
      # launchd config if requested
      if self.launchd_setup:
        # Daemon first (safer if upgrade fails)
        if self.is_system:
          if not os.path.isdir(self._LaunchDaemonConfigDir()):
            os.makedirs(self._LaunchDaemonConfigDir())
            # Again set permissions only if we created it, but if we did use
            # standard permission from a default OS install.
            os.chown(self._LaunchDaemonConfigDir(), 0, 0)
            os.chmod(self._LaunchDaemonConfigDir(), 0755)
          self._InstallPlist(self._DaemonPlistFileName(),
                                     self._LaunchDaemonConfigDir())
        # Agent launchd
        if self._AgentPlistFileName() is not None:
          if not os.path.isdir(self._LaunchAgentConfigDir()):
            os.makedirs(self._LaunchAgentConfigDir())
            # /Library/LaunchAgents is a OS directory, use permissions from
            # default OS install, but only if we created it.
            if self.is_system:
              os.chown(self._LaunchAgentConfigDir(), 0, 0)
              os.chmod(self._LaunchAgentConfigDir(), 0755)
          self._InstallPlist(self._AgentPlistFileName(),
                                     self._LaunchAgentConfigDir())
        # Agent login item remove/restore. Removal prior to add
        # so that removal happens in Tiger -> Leopard upgrade case and
        # we do not duplicate entries.
        self._RemoveAgentLoginItem()
        self._InstallAgentLoginItem()
    finally:
      if self.is_system:
        self._ClearSystemInstallPermissions()
    # If requested, start our jobs, failures treated as errors.
    if self.launchd_setup and self.launchd_jobs:
      if self.is_system:
        self._ChangeDaemonRunStatus(True, False)
      self._ChangeAgentRunStatus(True, False)

  def LockdownKeystone(self):
    """Prevent Keystone from ever self-uninstalling.

    This is necessary for a System Keystone used for Trusted Tester support.
    We do this by installing (and never uninstalling) a system ticket.
    """
    if self.is_system:
      self._SetSystemInstallPermissions()
    try:
      ksadmin_path = self._KsadminPath()
      if not ksadmin_path:
        raise Error(self.package, self.root, 'ksadmin not available')
      cmd = [ksadmin_path,
             # store is specified explicitly so unit tests work
             '--store', os.path.join(self._KeystoneTicketStorePath(),
                                     'Keystone.ticketstore'),
             '--register',
             '--productid', LOCKDOWN_TICKET,
             '--version', '1.0',
             '--xcpath', '/',
             '--url', self._KeystoneTicketURL()]
      (result, out, errout) = self.RunCommand(cmd)
      if result != 0:
        raise Error(self.package, self.root,
            'Keystone ticket install failed (%d): %s' % (result, errout))
    finally:
      if self.is_system:
        self._ClearSystemInstallPermissions()

  def Uninstall(self):
    """Perform a complete uninstall (uninstall leaves tickets in place)"""
    # On uninstall if we are not in self-destruct stop all processes but
    # ignore failure (may not be running). On a non-self destruct case we do
    # this first since it avoids race conditions on caches and pref writes
    if not self.self_destruct and self.launchd_setup and self.launchd_jobs:
      self._ChangeAgentRunStatus(False, True)
      if self.is_system:
        self._ChangeDaemonRunStatus(False, True)
    # Perform file removals. In self-destruct case the processes may still
    # be running.
    if self.is_system:
      self._SetSystemInstallPermissions()
    try:
      # Remove plist files unless blocked
      if self.launchd_setup:
        # In self-destruct mode we still need these plists for launchctl
        if not self.self_destruct:
          if self._AgentPlistFileName() is not None:
            agent_plist = os.path.join(self._LaunchAgentConfigDir(),
                                       self._AgentPlistFileName())
            if os.path.exists(agent_plist):
              os.unlink(agent_plist)
          daemon_plist = os.path.join(self._LaunchDaemonConfigDir(),
                                      self._DaemonPlistFileName())
          if os.path.exists(daemon_plist):
            os.unlink(daemon_plist)
        # Self-destruct or not, we can remove login item (Tiger)
        self._RemoveAgentLoginItem()
      # Unregister Keystone ticket (if installed at all).
      if os.path.exists(self._KeystoneBundlePath()):
        ksadmin_path = self._KsadminPath()
        if not ksadmin_path or not os.path.exists(ksadmin_path):
          raise Error(self.package, self.root, 'ksadmin not available')
        cmd = [ksadmin_path,
               # store is specified explicitly so unit tests work
               '--store', os.path.join(self._KeystoneTicketStorePath(),
                                       'Keystone.ticketstore'),
               '--delete', '--productid', 'com.google.Keystone']
        (result, out, errout) = self.RunCommand(cmd)
        if result != 0 and errout.find('No ticket to delete') == -1:
          raise Error(self.package, self.root,
              'Keystone ticket uninstall failed (%d): %s' % (result, errout))
      # Remove the Keystone bundle
      if os.path.exists(self._KeystoneBundlePath()):
        shutil.rmtree(self._KeystoneBundlePath())
      # Clean up caches. Race condition here if self-destructing, but unlikely
      # and we'll just leak a cache dir.
      if os.path.exists(self._LibraryCachesDirPath()):
        caches = glob.glob(os.path.join(self._LibraryCachesDirPath(),
                                        'com.google.Keystone.*'))
        caches.extend(glob.glob(os.path.join(self._LibraryCachesDirPath(),
                                        'com.google.UpdateEngine.*')))
        caches.extend(glob.glob(os.path.join(self._LibraryCachesDirPath(),
                                        'UpdateEngine-Temp')))
        for cache_item in caches:
          if os.path.isdir(cache_item):
            shutil.rmtree(cache_item, True)  # Ignore cache deletion errors
          else:
            try:
              os.unlink(cache_item)
            except OSError:
              pass
      # Clean up preferences, this prevents old installations from propagating
      # dates (like uninstall embargo time) forward in a complete uninstall/
      # reinstall scenario. Again, race condition here for self-destruct case
      # but the risk is minor and only leaks a pref file.
      if self.is_system:
        agent_pref_path = os.path.join(pwd.getpwuid(self.agent_job_uid)[5],
                                       'Library/Preferences/'
                                       'com.google.Keystone.Agent.plist')
      else:
        agent_pref_path = os.path.expanduser('~/Library/Preferences/'
                                             'com.google.Keystone.Agent.plist')
      if os.path.exists(agent_pref_path):
        os.unlink(agent_pref_path)
    finally:
      if self.is_system:
        self._ClearSystemInstallPermissions()
    # Remove receipts
    self.RemoveReceipts()
    # With all other files removed, cleanup processes and job control files in
    # the self-destruct case. This will presumably kill our parent, so after
    # this no one is listening for our errors. We do it as late as possible.
    if self.self_destruct:
      if self.launchd_setup and self.launchd_jobs:
        self._ChangeAgentRunStatus(False, True)
        if self.is_system:
          self._ChangeDaemonRunStatus(False, True)
      if self.is_system:
        self._SetSystemInstallPermissions()
      try:
        # We needed these plists to stop the agent and daemon. No one is
        # listening to errors, but failure only leaves a stale launchctl file
        # (actual program files removed above)
        if self._AgentPlistFileName() is not None:
          agent_plist = os.path.join(self._LaunchAgentConfigDir(),
                                     self._AgentPlistFileName())
          if os.path.exists(agent_plist):
            os.unlink(agent_plist)
        daemon_plist = os.path.join(self._LaunchDaemonConfigDir(),
                                    self._DaemonPlistFileName())
        if os.path.exists(daemon_plist):
          os.unlink(daemon_plist)
      finally:
        if self.is_system:
          self._ClearSystemInstallPermissions()

  def Nuke(self):
    """Perform an uninstall and remove all files (including tickets)"""
    # Uninstall
    self.Uninstall()
    # Nuke what's left
    if self.is_system:
      self._SetSystemInstallPermissions()
    try:
      # Remove whole Keystone tree
      if os.path.exists(self._KeystoneDirPath()):
        shutil.rmtree(self._KeystoneDirPath())
    finally:
      if self.is_system:
        self._ClearSystemInstallPermissions()

  def RemoveReceipts(self):
    """Remove receipts from Apple's package database, allowing downgrade or
    reinstall."""
    # Only works on system installs
    if self.is_system:
      self._SetSystemInstallPermissions()
      try:
        # In theory we should only handle old-style receipts on older OS
        # versions. However, we don't know the upgrade history of the machine.
        # So we try all variants.
        if os.path.isdir('/Library/Receipts/Keystone.pkg'):
          shutil.rmtree('/Library/Receipts/Keystone.pkg', True)
        if os.path.exists('/Library/Receipts/Keystone.pkg'):
          try:
            os.unlink('/Library/Receipts/Keystone.pkg')
          except OSError:
            pass
        if os.path.isdir('/Library/Receipts/UninstallKeystone.pkg'):
          shutil.rmtree('/Library/Receipts/UninstallKeystone.pkg', True)
        if os.path.exists('/Library/Receipts/UninstallKeystone.pkg'):
          try:
            os.unlink('/Library/Receipts/UninstallKeystone.pkg')
          except OSError:
            pass
        if os.path.isdir('/Library/Receipts/NukeKeystone.pkg'):
          shutil.rmtree('/Library/Receipts/NukeKeystone.pkg', True)
        if os.path.exists('/Library/Receipts/NukeKeystone.pkg'):
          try:
            os.unlink('/Library/Receipts/NukeKeystone.pkg')
          except OSError:
            pass
        # pkgutil where appropriate (ignoring results)
        if os.path.exists('/usr/sbin/pkgutil'):
          self.RunCommand(['/usr/sbin/pkgutil', '--forget',
                           'com.google.pkg.Keystone'])
          self.RunCommand(['/usr/sbin/pkgutil', '--forget',
                           'com.google.pkg.UninstallKeystone'])
          self.RunCommand(['/usr/sbin/pkgutil', '--forget',
                           'com.google.pkg.NukeKeystone'])
      finally:
        self._ClearSystemInstallPermissions()

  def FixupProducts(self):
    """Attempt to repair any products might be broken."""
    if self.is_system:
      self._SetSystemInstallPermissions()
    try:
      # Remove the (original) Google Updater manifest files.  Stale manifest
      # caches prevent Updater from checking for updates and downloading its
      # auto-uninstall package. Stomp those files everywhere we can.
      try:
        os.unlink(os.path.expanduser('~/Library/Application Support/'
                                     'Google/SoftwareUpdates/manifest.xml'))
      except OSError:
        pass
      if self.agent_job_uid is not None:
        try:
          os.unlink(os.path.join(pwd.getpwuid(self.agent_job_uid)[5],
                                 'Library/Application Support/'
                                 'Google/SoftwareUpdates/manifest.xml'))
        except OSError:
          pass
      try:
        os.unlink(os.path.join(self.root, 'Library/Application Support/'
                               'Google/SoftwareUpdates/manifest.xml'))
      except OSError:
        pass
      try:
        os.unlink('/Library/Caches/Google/SoftwareUpdates/manifest.xml')
      except OSError:
        pass

      # Google Talk Plugin 1.0.15.1351 can have its existence checker
      # pointing to a deleted directory.  Fix up the xc so it'll update
      # next time.
      if self.is_system:
        # See if there's a talk plugin ticket.
        ksadmin_path = self._KsadminPath()
        if ksadmin_path and os.path.exists(ksadmin_path):
          (result, out, errout) = self.RunCommand([ksadmin_path, '--productid',
              'com.google.talkplugin', '-p'])
          if out.find('1.0.15.1351') != -1:
            # Fix the ticket by reregistering it.
            # We can only get here if 1.0.15.1351 is the current version, so
            # it's safe to use that version.
            (result, out, errout) = self.RunCommand([ksadmin_path, '--register',
                '--productid', 'com.google.talkplugin',
                '--xcpath',
                '/Library/Internet Plug-Ins/googletalkbrowserplugin.plugin',
                '--version', '1.0.15.1351',
                '--url', 'https://tools.google.com/service/update2'])
    finally:
      if self.is_system:
        self._ClearSystemInstallPermissions()

# -------------------------------------------------------------------------

class KeystoneInstallTiger(KeystoneInstall):

  """Like KeystoneInstall, but overrides a few methods to support 10.4"""

  def _AgentPlistFileName(self):
    return None

  def _DaemonPlistFileName(self):
    return 'com.google.keystone.daemon4.plist'

  def _InstallAgentLoginItem(self):
    # This will write to the Library domain as root/wheel, which is OK because
    # permissions on /Library/Preferences still allow admin group to modify
    if self.is_system:
      domain = '/Library/Preferences/loginwindow'
    else:
      domain = 'loginwindow'
    (result, out, errout) = self.RunCommand(
        ['/usr/bin/defaults', 'write', domain,
         'AutoLaunchedApplicationDictionary', '-array-add',
         '{Hide = 1; Path = "%s"; }' % self._KeystoneAgentPath()])
    if result == 0:
      return
    # An empty AutoLaunchedApplicationDictionary is an empty string,
    # not an empty array, in which case -array-add chokes.  There is
    # no easy way to do a typeof(AutoLaunchedApplicationDictionary)
    # for a plist. Our solution is to catch the error and try a
    # different way.
    (result, out, errout) = self.RunCommand(
        ['/usr/bin/defaults', 'write', domain,
         'AutoLaunchedApplicationDictionary', '-array',
         '{Hide = 1; Path = "%s"; }' % self._KeystoneAgentPath()])
    if result != 0:
      raise Error(self.package, self.root,
                  'Keystone agent login item in domain "%s" failed (%d): %s' %
                  (domain, result, errout))

  def _ChangeAgentRunStatus(self, start, ignore_failure):
    """Start the agent as a normal (non-launchd) process on Tiger."""
    if self.is_system:
      self._SetSystemInstallPermissions()
    try:
      # Start
      if start:
        if self.is_system:
          # Tiger 'sudo' has problems with numeric uid so use username (man
          # page wrong)
          username = pwd.getpwuid(self.agent_job_uid)[0]
          (result, out, errout) = self.RunCommand(['/usr/bin/sudo',
                                                   '-u', username,
                                                   '/usr/bin/open',
                                                   self._KeystoneAgentPath()])
          if not ignore_failure and result != 0:
            raise Error(self.package, self.root,
                        'Failed to start system agent for uid %d (%d): %s' %
                        (self.agent_job_uid, result, errout))
        else:
          (result, out, errout) = self.RunCommand(['/usr/bin/open',
                                                   self._KeystoneAgentPath()])
          if not ignore_failure and result != 0:
            raise Error(self.package, self.root,
                        'Failed to start user agent (%d): %s' %
                        (result, errout))
      # Stop
      else:
        if self.is_system:
          cmd = ['/usr/bin/killall', '-u', str(self.agent_job_uid),
                 self._AgentProcessName()]
        else:
          cmd = ['/usr/bin/killall', self._AgentProcessName()]
        (result, out, errout) = self.RunCommand(cmd)
        if (not ignore_failure and result != 0 and
            out.find('No matching processes') == -1):
          raise Error(self.package, self.root,
                      'Failed to kill agent (%d): %s' % (result, errout))
    finally:
      if self.is_system:
        self._ClearSystemInstallPermissions()

  def _ClearQuarantine(self, path):
    """Remove LaunchServices quarantine attributes from a file hierarchy."""
    # Tiger does not implement quarantine (http://support.apple.com/kb/HT3662)
    return


# -------------------------------------------------------------------------

class Keystone(object):

  """Top-level interface for Keystone install and uninstall.

  Attributes:
    install_class: KeystoneInstall subclass to use for installation
    installer: KeystoneInstall instance (system or user)
  """

  def __init__(self, package, root, launchd_setup, start_jobs, self_destruct):
    # Sanity
    if package:
      package = os.path.abspath(os.path.expanduser(package))
      CheckOnePath(package, stat.S_IRUSR)
    if root:
      expanded_root = os.path.abspath(os.path.expanduser(root))
      assert (expanded_root and
              len(expanded_root) > 0), 'Root is empty after expansion'
      # Force user-supplied root to pre-exist, this was a side effect of
      # prior versions of the code and the tests assume its part of the contract
      CheckOnePath(root, stat.S_IWUSR)
    # Setup installer instances
    self.install_class = KeystoneInstall
    if self._IsTiger():
      self.install_class = KeystoneInstallTiger
    if self._IsPrivilegedInstall():
      # Install using privileges on behalf of other user (for agent start)
      install_uid = self._LocalUserUID()
      if root is not None:
        self.installer = self.install_class(package, True, install_uid, root,
                                            launchd_setup, start_jobs,
                                            self_destruct)
      else:
        self.installer = self.install_class(package, True, install_uid,
                                            self._DefaultRootForUID(0),
                                            launchd_setup, start_jobs,
                                            self_destruct)
    else:
      # Non-system install, no attempt at privilege changes
      if root is not None:
        self.installer = self.install_class(package, False, None, root,
                                            launchd_setup, start_jobs,
                                            self_destruct)
      else:
        self.installer = self.install_class(package, False, None,
                                            self._DefaultRootForUID(
                                                self._LocalUserUID()),
                                            launchd_setup, start_jobs,
                                            self_destruct)

  def _LocalUserUID(self):
    """Return the UID of the local (non-root) user who initiated this
    install/uninstall.  If we can't figure it out, default to the user
    on conosle.  We don't want to default to console user in case a
    FUS happens in the middle of install or uninstall."""
    uid = os.geteuid()
    if uid != 0:
      return uid
    else:
      return os.stat('/dev/console')[stat.ST_UID]

  def _IsLeopardOrLater(self):
    """Return True if we're on 10.5 or later; else return False."""
    global FORCE_TIGER
    if FORCE_TIGER:
      return False
    # Ouch!  platform.mac_ver() returns strange results.
    # ('10.7', ('', '', ''), 'i386')        - 10.7, python2.7
    # ('10.7.0', ('', '', ''), 'i386')      - 10.7, python2.5 or python2.6
    # ('10.6.7', ('', '', ''), 'i386')      - 10.6, python2.5 or python2.6
    # ('10.5.1', ('', '', ''), 'i386')      - 10.5, python2.4 or python2.5
    # ('', ('', '', ''), '')                - 10.4, python2.3 (also 2.4)
    (vers, ignored1, ignored2) = platform.mac_ver()
    splits = vers.split('.')
    # Try to break down a proper version number
    if ((len(splits) == 2) or (len(splits) == 3)) and (splits[1] >= '5'):
      return True
    # Tiger is rare these days, so unless we're on 2.3 build of Python
    # assume we must be newer.
    if (((sys.version_info[0] == 2) and (sys.version_info[1] == 3)) or
        ((sys.version_info[0] == 2) and (sys.version_info[1] == 4) and
         (vers == ''))):
      return False
    else:
      return True

  def _IsTiger(self):
    """Return the boolean opposite of IsLeopardOrLater()."""
    if self._IsLeopardOrLater():
      return False
    else:
      return True

  def _IsPrivilegedInstall(self):
    """Return True if this is a privileged (root) install."""
    if os.geteuid() == 0:
      return True
    else:
      return False

  def _DefaultRootForUID(self, uid):
    """For the given UID, return the default install root for Keystone (where
    is is, or where it should be, installed)."""
    if uid == 0:
      return '/'
    else:
      return pwd.getpwuid(uid)[5]

  def _ShouldInstall(self):
    """Return True if we should on install.

    Possible reasons for punting (returning False):
    1) This is a System Keystone install and the installed System
       Keystone has a smaller version.
    2) This is a User Keystone and there is a System Keystone
       installed (of any version).
    3) This is a User Keystone and the installed User Keystone has a
       smaller version.
    """
    if self._IsPrivilegedInstall():
      if self.installer.IsMyVersionGreaterThanInstalledVersion():
        return True
      else:
        return False
    else:
      # User install, need to check if system install exists
      system_checker = self.install_class(None, False, None,
                                          self._DefaultRootForUID(0),
                                          False, False, False)
      if system_checker.InstalledKeystoneBundleVersion() != None:
        return False
      # Check just user version
      if self.installer.IsMyVersionGreaterThanInstalledVersion():
        return True
      else:
        return False

  def Install(self, force, lockdown):
    """Public install interface.

      force: If True, no version check is performed.
      lockdown: if True, install a special ticket to lock down Keystone
                and prevent uninstall.  This will happen even if an install
                of Keystone itself is not needed.
    """
    if force or self._ShouldInstall():
      self.installer.Install()
    # possibly lockdown even if we don't need to install
    if lockdown:
      self.installer.LockdownKeystone()

  def Uninstall(self):
    """Uninstall, which has the effect of preparing this machine for a new
    install. Although similar, it is NOT as comprehensive as a nuke.
    """
    self.installer.Uninstall()

  def Nuke(self):
    """Public nuke interface. Typically only used for testing."""
    self.installer.Nuke()

  def RemoveReceipts(self):
    """Public receipt removal interface. Used by uninstall, and to allow
    downgraades of system installations."""
    self.installer.RemoveReceipts()

  def FixupProducts(self):
    """Attempt to repair any products might have broken tickets."""
    self.installer.FixupProducts()

# -------------------------------------------------------------------------

def PrintUse():
  print 'Use: '
  print ' [--install PKG]     Install keystone using PKG as the source.'
  print ' [--root ROOT]       Use ROOT as the dest for an install. Optional.'
  print ' [--uninstall]       Remove Keystone program files but do NOT delete '
  print '                       the ticket store.'
  print ' [--nuke]            Remove Keystone and all tickets.'
  print ' [--remove-receipts] Remove Keystone package receipts, allowing for '
  print '                       downgrade (system install only)'
  print ' [--no-launchd]      Do NOT touch Keystone launchd plists or jobs,'
  print '                       for both install and uninstall. For test.'
  print ' [--no-launchdjobs]  Do NOT start/stop jobs, but do change launchd'
  print '                       plist files,for both install and uninstall.'
  print '                       For test.'
  print ' [--self-destruct]   Use if uninstall is triggered by process that '
  print '                       will be killed by uninstall.'
  print ' [--force]           Force an install no matter what. For test.'
  print ' [--forcetiger]      Pretend we are on Tiger (MacOSX 10.4). For test.'
  print ' [--lockdown]        Prevent Keystone from ever uninstalling itself.'
  print ' [--interval N]      Change agent plist to wake up every N seconds.'
  print ' [--help]            This message.'


def main():
  os.environ.clear()
  os.environ['PATH'] = '/bin:/sbin:/usr/bin:/usr/sbin:/usr/libexec'

  # Make sure AuthorizationExecuteWithPrivileges() is happy
  if os.getuid() and os.geteuid() == 0:
    os.setuid(os.geteuid())

  try:
    opts, args = getopt.getopt(sys.argv[1:], 'i:r:XunNhfI:',
                               ['install=', 'root=', 'nuke', 'uninstall',
                                'remove-receipts', 'no-launchd',
                                'no-launchdjobs', 'self-destruct', 'help',
                                'force', 'forcetiger', 'lockdown', 'interval='])
  except getopt.GetoptError:
    print 'Bad options.'
    PrintUse()
    sys.exit(1)

  root = None
  package = None
  nuke = False
  uninstall = False
  remove_receipts = False
  launchd_setup = True
  start_jobs = True
  self_destruct = False
  force = False
  lockdown = False  # If true, prevent uninstall by adding a "lockdown" ticket

  for opt, val in opts:
    if opt in ('-i', '--install'):
      package = val
    if opt in ('-r', '--root'):
      root = val
    if opt in ('-X', '--nuke'):
      nuke = True
    if opt in ('-u', '--uninstall'):
      uninstall = True
    if opt in ('--remove-receipts'):
      remove_receipts = True
    if opt in ('-n', '--no-launchd'):
      launchd_setup = False
    if opt in ('-N', '--no-launchdjobs'):
      start_jobs = False
    if opt in ('--self-destruct'):
      self_destruct = True
    if opt in ('-f', '--force'):
      force = True
    if opt in ('-T', '--forcetiger'):
      global FORCE_TIGER
      FORCE_TIGER = True
    if opt in ('--lockdown',):
      lockdown = True
    if opt in ('-I', '--interval'):
      global AGENT_START_INTERVAL
      AGENT_START_INTERVAL = int(val)
    if opt in ('-h', '--help'):
      PrintUse()
      sys.exit(0)

  if package is None and not nuke and not uninstall and not remove_receipts:
    print 'Must specify package path, uninstall, nuke, or remove-receipts'
    PrintUse()
    sys.exit(1)
  try:
    (vers, ignored1, ignored2) = platform.mac_ver()
    splits = vers.split('.')
    if (len(splits) == 3) and (int(splits[1]) < 4):
      print 'Requires Mac OS 10.4 or later'
      sys.exit(1)
  except:
    # 10.3 throws an exception for platform.mac_ver()
    print 'Requires Mac OS 10.4 or later'
    sys.exit(1)

  # Lock file to make sure only one Keystone install at once. We want to
  # share this lock amongst all users on the machine.
  lockfilename = '/tmp/.keystone_install_lock'
  oldmask = os.umask(0000)
  lockfile = os.open(lockfilename, os.O_CREAT | os.O_RDONLY | os.O_NOFOLLOW,
                     0444)
  os.umask(oldmask)
  # Lock, callers that cannot wait are expected to kill us.
  fcntl.flock(lockfile, fcntl.LOCK_EX)

  try:
    try:
      k = Keystone(package, root, launchd_setup, start_jobs, self_destruct)
      # Ordered by level of cleanup applied
      if nuke:
        k.Nuke()
      elif uninstall:
        k.Uninstall()
      elif remove_receipts:
        k.RemoveReceipts()
      else:
        k.Install(force, lockdown)
        k.FixupProducts()
    except Error, e:
      print e  # To conform to previous contract on this tool (see headerdoc)
      raise  # So that the backtrace ends up on stderr
  finally:
    os.close(lockfile)  # Lock file left around on purpose


if __name__ == "__main__":
  main()
