git-new-workdir.cmd
===================

SYNOPSIS
--------
[verse]
'git-new-workdir.cmd' <repository> <new_workdir> [branch]

If UAC is enabled:
runas /env /user:administrator "git-new-workdir.cmd <repository> <new_workdir> [branch]"


DESCRIPTION
-----------

'git-new-workdir.cmd' is a port of 'git-new-workdir' for Windows.  It is a 1:1
port to facilitate ongoing maintenance.

A port is necessary due to the following reasons:

1. cygwin/mingw32/msysgit do no support `ln -s`.

   The generated symlinks are not understood by Windows.

   http://stackoverflow.com/questions/18641864/git-bash-shell-fails-to-create-symbolic-links
   http://mingw.5.n7.nabble.com/symbolic-link-to-My-Documents-in-MSYS-td28492.html


2. `mklink` requires privilege escalation on Windows 7+.

   If UAC is enabled, and the current user is in the 'Administrators' group,
   then the 'SeCreateSymbolicLinkPrivilege' is force-removed upon user login.

   http://blog.rlucas.net/rants/dont-bother-with-symlinks-in-windows-7/
   http://stackoverflow.com/questions/15320550/secreatesymboliclinkprivilege-ignored-on-windows-8
   http://msdn.microsoft.com/en-us/library/bb530410.aspx
   http://serverfault.com/questions/397062/giving-select-windows-domain-users-symbolic-link-privilege
   http://social.msdn.microsoft.com/Forums/windowsdesktop/en-US/fa504848-a5ea-4e84-99b7-0eb4e469cbef/createsymboliclink-bug?forum=windowssdk
   http://social.msdn.microsoft.com/Forums/en-US/e967ab01-3136-4fda-9677-e5ecaaa2f694/configuring-symlink-support-in-win7?forum=os_fileservices
   http://social.technet.microsoft.com/Forums/windowsserver/en-US/d19ea008-4b8d-42bb-badf-f2105e5952a0/unable-to-grant-secreatesymboliclinkprivilege?forum=winserverfiles


3. Normally, `mklink` can be used via `runas` like this:

     runas /env /user:administrator "mklink target source"

   Theoretically, the 'git-new-workdir' script could be changed to conditionally
   call `mklink` depending on whether $OSTYPE returns "cygwin" or "msys", and
   changing the command to this:

     runas /env /user:administrator "bash -c \"cmd.exe /c mklink target source\""

   However, when executing `mklink` through a `cmd` invoked from a *nix shell
   script that is executed via `runas`, then the file permissions (ACL) of
   created links are broken: the links do not have an owner and write access is
   denied for all users.

   While that theoretically could be fixed by a subsequent ACL change like this:

     icacls %new_workdir% /grant "Users":F

   Doing so would be a security issue.


These problems can only be avoided with a full port of the script.

If UAC is enabled, the command line changes to:

  runas /env /user:administrator "git-new-workdir path\repo path\new\workdir"

The '/env' argument is important; failure to include it means:

1. Relative paths will not be resolved correctly (since the process would be
   executed in the home directory of the specified user)

2. All created links as well as all files in the new working directory checkout
   will not be writable by the current user.

