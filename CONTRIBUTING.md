How to Contribute to Git for Windows
====================================

Git was originally designed for Unix systems and still today, all the build tools for the Git
codebase assume you have standard Unix tools available in your path. If you have an open-source
mindset and want to start contributing to Git, but primarily use a Windows machine, then you may
have trouble getting started. This guide is for you.

Get the Source
--------------

Clone the [GitForWindows repository on GitHub](https://github.com/git-for-windows/git).
It is helpful to create your own fork for storing your development branches.

Windows uses different line endings than Unix systems. See
[this GitHub article on working with line endings](https://help.github.com/articles/dealing-with-line-endings/#refreshing-a-repository-after-changing-line-endings)
if you have trouble with line endings.

Build the Source
----------------

First, download and install the latest [Git for Windows SDK (64-bit)](https://github.com/git-for-windows/build-extra/releases/latest).
When complete, you can run the Git SDK, which creates a new Git Bash terminal window with
the additional development commands, such as `make`.

    As of time of writing, the SDK uses a different credential manager, so you may still want to use normal Git
    Bash for interacting with your remotes.  Alternatively, use SSH rather than HTTPS and
    avoid credential manager problems.

You should now be ready to type `make` from the root of your `git` source directory.
Here are some helpful variations:

* `make -j[N] DEVELOPER=1`: Compile new sources using up to N concurrent processes.
  The `DEVELOPER` flag turns on all warnings; code failing these warnings will not be
  accepted upstream ("upstream" = "the core Git project").
* `make clean`: Delete all compiled files.

When running `make`, you can use `-j$(nproc)` to automatically use the number of processors
on your machine as the number of concurrent build processes.

You can go deeper on the Windows-specific build process by reading the
[technical overview](https://github.com/git-for-windows/git/wiki/Technical-overview) or the
[guide to compiling Git with Visual Studio](https://github.com/git-for-windows/git/wiki/Compiling-Git-with-Visual-Studio).

## Building `git` on Windows with Visual Studio

The typical approach to building `git` is to use the standard `Makefile` with GCC, as
above. Developers working in a Windows environment may want to instead build with the
[Microsoft Visual C++ compiler and libraries toolset (MSVC)](https://blogs.msdn.microsoft.com/vcblog/2017/03/07/msvc-the-best-choice-for-windows/).
There are a few benefits to using MSVC over GCC during your development, including creating
symbols for debugging and [performance tracing](https://github.com/Microsoft/perfview#perfview-overview).

There are two ways to build Git for Windows using MSVC. Each have their own merits.

### Using SDK Command Line

Use one of the following commands from the SDK Bash window to build Git for Windows:

```
    make MSVC=1 -j12
    make MSVC=1 DEBUG=1 -j12
```

The first form produces release-mode binaries; the second produces debug-mode binaries.
Both forms produce PDB files and can be debugged.  However, the first is best for perf
tracing and the second is best for single-stepping.

You can then open Visual Studio and select File -> Open -> Project/Solution and select
the compiled `git.exe` file. This creates a basic solution and you can use the debugging
and performance tracing tools in Visual Studio to monitor a Git process. Use the Debug
Properties page to set the working directory and command line arguments.

Be sure to clean up before switching back to GCC (or to switch between debug and
release MSVC builds):

```
    make MSVC=1 -j12 clean
    make MSVC=1 DEBUG=1 -j12 clean
```

### Using `vs/master` Solution

If you prefer working in Visual Studio with a solution full of projects, then there is a
branch in Git for Windows called [`vs/master`](https://github.com/git-for-windows/git/branches).
This branch is kept up-to-date with the `master` branch, except it has one more commit that
contains the solution and project files. Read [the wiki page on this approach](https://github.com/git-for-windows/git/wiki/Compiling-Git-with-Visual-Studio) for more information.

I want to make a small warning before you start working on the `vs/master` branch. If you
create a new topic branch based on `vs/master`, you will need to rebase onto `master` before
you can submit a pull request. The commit at the tip of `vs/master` is not intended to ever
become part of the `master` branch. If you created a branch, `myTopic` based on `vs/master`,
then use the following rebase command to move it onto the `master` branch:

```
git rebase --onto master vs/master myTopic
```

What to Change?
---------------

Many new contributors ask: What should I start working on?

One way to win big with the open-source community is to look at the
[issues page](https://github.com/git-for-windows/git/issues) and see if there are any issues that
you can fix quickly, or if anything catches your eye.

You can also look at [the unofficial Chromium issues page](https://crbug.com/git) for
multi-platform issues. You can look at recent user questions on
[the Git mailing list](https://public-inbox.org/git).

Or you can "scratch your own itch", i.e. address an issue you have with Git. The team at Microsoft where the Git for Windows maintainer works, for example, is focused almost entirely on [improving performance](https://blogs.msdn.microsoft.com/devops/2018/01/11/microsofts-performance-contributions-to-git-in-2017/).
We approach our work by finding something that is slow and try to speed it up. We start our
investigation by reliably reproducing the slow behavior, then running that example using
the MSVC build and tracing the results in PerfView.

You could also think of something you wish Git could do, and make it do that thing! The
only concern I would have with this approach is whether or not that feature is something
the community also wants. If this excites you though, go for it! Don't be afraid to
[get involved in the mailing list](http://vger.kernel.org/vger-lists.html#git) early for
feedback on the idea.

Test Your Changes
-----------------

After you make your changes, it is important that you test your changes. Manual testing is
important, but checking and extending the existing test suite is even more important. You
want to run the functional tests to see if you broke something else during your change, and
you want to extend the functional tests to be sure no one breaks your feature in the future.

### Functional Tests

Navigate to the `t/` directory and type `make` to run all tests or use `prove` as
[described in the Git for Windows wiki](https://github.com/git-for-windows/git/wiki/Building-Git):

```
prove -j12 --state=failed,save ./t[0-9]*.sh
```

You can also run each test directly by running the corresponding shell script with a name
like `tNNNN-descriptor.sh`.

If you are adding new functionality, you may need to create unit tests by creating
helper commands that test a very limited action. These commands are stored in `t/helpers`.
When adding a helper, be sure to add a line to `t/Makefile` and to the `.gitignore` for the
binary file you add. The Git community prefers functional tests using the full `git`
executable, so try to exercise your new code using `git` commands before creating a test
helper.

To find out why a test failed, repeat the test with the `-x -v -d -i` options and then
navigate to the appropriate "trash" directory to see the data shape that was used for the
test failed step.

Read [`t/README`](t/README) for more details.

### Performance Tests

If you are working on improving performance, you will need to be acquainted with the
performance tests in `t/perf`. There are not too many performance tests yet, but adding one
as your first commit in a patch series helps to communicate the boost your change provides.

To check the change in performance across multiple versions of `git`, you can use the
`t/perf/run` script. For example, to compare the performance of `git rev-list` across the
`core/master` and `core/next` branches compared to a `topic` branch, you can run

```
cd t/perf
./run core/master core/next topic -- p0001-rev-list.sh
```

You can also set certain environment variables to help test the performance on different
repositories or with more repetitions. The full list is available in
[the `t/perf/README` file](t/perf/README),
but here are a few important ones:

```
GIT_PERF_REPO=/path/to/repo
GIT_PERF_LARGE_REPO=/path/to/large/repo
GIT_PERF_REPEAT_COUNT=10
```

When running the performance tests on Linux, you may see a message "Can't locate JSON.pm in
@INC" and that means you need to run `sudo cpanm install JSON` to get the JSON perl package.

For running performance tests, it can be helpful to set up a few repositories with strange
data shapes, such as:

**Many objects:** Clone repos such as [Kotlin](https://github.com/jetbrains/kotlin), [Linux](https://github.com/torvalds/linux), or [Android](https://source.android.com/setup/downloading).

**Many pack-files:** You can split a fresh clone into multiple pack-files of size at most
16MB by running `git repack -adfF --max-pack-size=16m`. See the
[`git repack` documentation](https://git-scm.com/docs/git-repack) for more information.
You can count the number of pack-files using `ls .git/objects/pack/*.pack | wc -l`.

**Many loose objects:** If you already split your repository into multiple pack-files, then
you can pick one to split into loose objects using `cat .git/objects/pack/[id].pack | git unpack-objects`;
delete the `[id].pack` and `[id].idx` files after this. You can count the number of loose
bjects using `ls .git/objects/??/* | wc -l`.

**Deep history:** Usually large repositories also have deep histories, but you can use the
[test-many-commits-1m repo](https://github.com/cirosantilli/test-many-commits-1m/) to
target deep histories without the overhead of many objects. One issue with this repository:
there are no merge commits, so you will need to use a different repository to test a "wide"
commit history.

**Large Index:** You can generate a large index and repo by using the scripts in
`t/perf/repos`.  There are two scripts. `many-files.sh` which will generate a repo with
same tree and blobs but different paths.  Using `many-files.sh -d 5 -w 10 -f 9` will create
a repo with ~1 million entries in the index. `inflate-repo.sh` will use an existing repo
and copy the current work tree until it is a specified size.

Test Your Changes on Linux
--------------------------

It can be important to work directly on the [core Git codebase](https://github.com/git/git),
such as a recent commit into the `master` or `next` branch that has not been incorporated
into Git for Windows. Also, it can help to run functional and performance tests on your
code in Linux before submitting patches to the mailing list, which focuses on many platforms.
The differences between Windows and Linux are usually enough to catch most cross-platform
issues.

### Using the Windows Subsystem for Linux

The [Windows Subsystem for Linux (WSL)](https://docs.microsoft.com/en-us/windows/wsl/install-win10)
allows you to [install Ubuntu Linux as an app](https://www.microsoft.com/en-us/store/p/ubuntu/9nblggh4msv6)
that can run Linux executables on top of the Windows kernel. Internally,
Linux syscalls are interpreted by the WSL, everything else is plain Ubuntu.

First, open WSL (either type "Bash" in Cortana, or execute "bash.exe" in a CMD window).
Then install the prerequisites, and `git` for the initial clone:

```
sudo apt-get update
sudo apt-get install git gcc make libssl-dev libcurl4-openssl-dev \
		     libexpat-dev tcl tk gettext git-email zlib1g-dev
```

Then, clone and build:

```
git clone https://github.com/git-for-windows/git
cd git
git remote add -f upstream https://github.com/git/git
make
```

Be sure to clone into `/home/[user]/` and not into any folder under `/mnt/?/` or your build
will fail due to colons in file names.

### Using a Linux Virtual Machine with Hyper-V

If you prefer, you can use a virtual machine (VM) to run Linux and test your changes in the
full environment. The test suite runs a lot faster on Linux than on Windows or with the WSL.
You can connect to the VM using an SSH terminal like
[PuTTY](https://www.chiark.greenend.org.uk/~sgtatham/putty/).

The following instructions are for using Hyper-V, which is available in some versions of Windows.
There are many virtual machine alternatives available, if you do not have such a version installed.

* [Download an Ubuntu Server ISO](https://www.ubuntu.com/download/server).
* Open [Hyper-V Manager](https://docs.microsoft.com/en-us/virtualization/hyper-v-on-windows/quick-start/enable-hyper-v).
* [Set up a virtual switch](https://docs.microsoft.com/en-us/virtualization/hyper-v-on-windows/quick-start/connect-to-network)
  so your VM can reach the network.
* Select "Quick Create", name your machine, select the ISO as installation source, and un-check
  "This virtual machine will run Windows."
* Go through the Ubuntu install process, being sure to select to install OpenSSH Server.
* When install is complete, log in and check the SSH server status with `sudo service ssh status`.
    * If the service is not found, install with `sudo apt-get install openssh-server`.
    * If the service is not running, then use `sudo service ssh start`.
* Use `shutdown -h now` to shutdown the VM, go to the Hyper-V settings for the VM, expand Network Adapter
  to select "Advanced Features", and set the MAC address to be static (this can save your VM from losing
  network if shut down incorrectly).
* Provide as many cores to your VM as you can (for parallel builds).
* Restart your VM, but do not connect.
* Use `ssh` in Git Bash, download [PuTTY](http://www.putty.org/), or use your favorite SSH client to connect to the VM through SSH.

In order to build and use `git`, you will need the following libraries via `apt-get`:

```
sudo apt-get update
sudo apt-get install git gcc make libssl-dev libcurl4-openssl-dev \
                     libexpat-dev tcl tk gettext git-email zlib1g-dev
```

To get your code from your Windows machine to the Linux VM, it is easiest to push the branch to your fork of Git and clone your fork in the Linux VM.

Don't forget to set your `git` config with your preferred name, email, and editor.

Polish Your Commits
-------------------

Before submitting your patch, be sure to read the [coding guidelines](https://github.com/git/git/blob/master/Documentation/CodingGuidelines)
and check your code to match as best you can. This can be a lot of effort, but it saves
time during review to avoid style issues.

The other possibly major difference between the mailing list submissions and GitHub PR workflows
is that each commit will be reviewed independently. Even if you are submitting a
patch series with multiple commits, each commit must stand on it's own and be reviewable
by itself. Make sure the commit message clearly explain the why of the commit not the how.
Describe what is wrong with the current code and how your changes have made the code better.

When preparing your patch, it is important to put yourself in the shoes of the Git community.
Accepting a patch requires more justification than approving a pull request from someone on
your team. The community has a stable product and is responsible for keeping it stable. If
you introduce a bug, then they cannot count on you being around to fix it. When you decided
to start work on a new feature, they were not part of the design discussion and may not
even believe the feature is worth introducing.

Questions to answer in your patch message (and commit messages) may include:
* Why is this patch necessary?
* How does the current behavior cause pain for users?
* What kinds of repositories are necessary for noticing a difference?
* What design options did you consider before writing this version? Do you have links to
  code for those alternate designs?
* Is this a performance fix? Provide clear performance numbers for various well-known repos.

Here are some other tips that we use when cleaning up our commits:

* Commit messages should be wrapped at 76 columns per line (or less; 72 is also a
  common choice).
* Make sure the commits are signed off using `git commit (-s|--signoff)`. See
  [SubmittingPatches](https://github.com/git/git/blob/v2.8.1/Documentation/SubmittingPatches#L234-L286)
  for more details about what this sign-off means.
* Check for whitespace errors using `git diff --check [base]...HEAD` or `git log --check`.
* Run `git rebase --whitespace=fix` to correct upstream issues with whitespace.
* Become familiar with interactive rebase (`git rebase -i`) because you will be reordering,
  squashing, and editing commits as your patch or series of patches is reviewed.
* Make sure any shell scripts that you add have the executable bit set on them.  This is
  usually for test files that you add in the `/t` directory.  You can use
  `git add --chmod=+x [file]` to update it. You can test whether a file is marked as executable
  using `git ls-files --stage \*.sh`; the first number is 100755 for executable files.
* Your commit titles should match the "area: change description" format. Rules of thumb:
    * Choose "<area>: " prefix appropriately.
    * Keep the description short and to the point.
    * The word that follows the "<area>: " prefix is not capitalized.
    * Do not include a full-stop at the end of the title.
    * Read a few commit messages -- using `git log origin/master`, for instance -- to
      become acquainted with the preferred commit message style.
* Build source using  `make DEVELOPER=1` for extra-strict compiler warnings.

Submit Your Patch
-----------------

Git for Windows [accepts pull requests on GitHub](https://github.com/git-for-windows/git/pulls), but
these are reserved for Windows-specific improvements. For core Git, submissions are accepted on
[the Git mailing list](https://public-inbox.org/git).

### Configure Git to Send Emails

There are a bunch of options for configuring the `git send-email` command. These options can
be found in the documentation for
[`git config`](https://git-scm.com/docs/git-config) and
[`git send-email`](https://git-scm.com/docs/git-send-email).

```
git config --global sendemail.smtpserver <smtp server>
git config --global sendemail.smtpserverport 587
git config --global sendemail.smtpencryption tls
git config --global sendemail.smtpuser <email address>
```

To avoid storing your password in the config file, store it in the Git credential manager:

```
$ git credential fill
protocol=smtp
host=<stmp server>
username=<email address>
password=password
```

Before submitting a patch, read the [Git documentation on submitting patches](https://github.com/git/git/blob/master/Documentation/SubmittingPatches).

To construct a patch set, use the `git format-patch` command. There are three important options:

* `--cover-letter`: If specified, create a `[v#-]0000-cover-letter.patch` file that can be
  edited to describe the patch as a whole. If you previously added a branch description using
  `git branch --edit-description`, you will end up with a 0/N mail with that description and
  a nice overall diffstat.
* `--in-reply-to=[Message-ID]`: This will mark your cover letter as replying to the given
  message (which should correspond to your previous iteration). To determine the correct Message-ID,
  find the message you are replying to on [public-inbox.org/git](https://public-inbox.org/git) and take
  the ID from between the angle brackets.

* `--subject-prefix=[prefix]`: This defaults to [PATCH]. For subsequent iterations, you will want to
  override it like `--subject-prefix="[PATCH v2]"`.  You can also use the `-v` option to have it
  automatically generate the version number in the patches.

If you have multiple commits and use the `--cover-letter` option be sure to open the
`0000-cover-letter.patch` file to update the subject and add some details about the overall purpose
of the patch series.

### Examples

To generate a single commit patch file:
```
git format-patch -s -o [dir] -1
```
To generate four patch files from the last three commits with a cover letter:
```
git format-patch --cover-letter -s -o [dir] HEAD~4
```
To generate version 3 with four patch files from the last four commits with a cover letter:
```
git format-patch --cover-letter -s -o [dir] -v 3 HEAD~4
```

### Submit the Patch

Run [`git send-email`](https://git-scm.com/docs/git-send-email), starting with a test email:

```
git send-email --to=yourself@address.com  [dir with patches]/*.patch
```

After checking the receipt of your test email, you can send to the list and to any
potentially interested reviewers.

```
git send-email --to=git@vger.kernel.org --cc=<email1> --cc=<email2> [dir with patches]/*.patch
```

To submit a nth version patch (say version 3):

```
git send-email --to=git@vger.kernel.org --cc=<email1> --cc=<email2> \
    --in-reply-to=<the message id of cover letter of patch v2> [dir with patches]/*.patch
```
