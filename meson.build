# Meson build system
# ==================
#
# The Meson build system is an alternative to our Makefile that you can use to
# build, test and install Git. Using Meson results in a couple of benefits:
#
#  - Out-of-tree builds.
#  - Better integration into IDEs.
#  - Easy-to-use autoconfiguration of available features on your system.
#
# To use Meson from the command line you need to have both Meson and Ninja
# installed. Alternatively, if you do not have Python available on your system,
# you can also use Muon instead of Meson and Samurai instead of Ninja, both of
# which are drop-ins replacement that only depend on C.
#
# Basic usage
# ===========
#
# In the most trivial case, you can configure, build and install Git like this:
#
#  1. Set up the build directory. This only needs to happen once per build
#     directory you want to have. You can also configure multiple different
#     build directories with different configurations.
#
#      $ meson setup build/
#
#     The build directory gets ignored by Git automatically as Meson will write
#     a ".gitignore" file into it. From hereon, we will assume that you execute
#     commands inside this build directory.
#
# 2. Compile Git. You can either use Meson, Ninja or Samurai to do this, so all
#    of the following invocations are equivalent:
#
#      $ meson compile
#      $ ninja
#      $ samu
#
#   The different invocations should ultimately not make much of a difference.
#   Using Meson also works with other generators though, like when the build
#   directory has been set up for use with Microsoft Visual Studio.
#
#   Ninja and Samurai use multiple jobs by default, scaling with the number of
#   processor cores available. You can pass the `-jN` flag to change this.
#
#   Meson automatically picks up ccache and sccache when these are installed
#   when setting up the build directory. You can override this behaviour when
#   setting up the build directory by setting the `CC` environment variable to
#   your desired compiler.
#
# 3. Execute tests. Again, you can either use Meson, Ninja or Samurai to do this:
#
#      $ meson test
#      $ ninja test
#      $ samu test
#
#   It is recommended to use Meson in this case though as it also provides you
#   additional features that the other build systems don't have available.
#   You can e.g. pass additional arguments to the test executables or run
#   individual tests:
#
#      # Execute the t0000-basic integration test and t-reftable-stack unit test.
#      $ meson test t0000-basic t-reftable-stack
#
#      # Execute all reftable unit tests.
#      $ meson test t-reftable-*
#
#      # Execute all tests and stop with the first failure.
#      $ meson test --maxfail 1
#
#      # Execute single test interactively such that features like `debug ()` work.
#      $ meson test -i --test-args='-ix' t1400-update-ref
#
#      # Execute all benchmarks.
#      $ meson test -i --benchmark
#
#      # Execute single benchmark.
#      $ meson test -i --benchmark p0000-*
#
#   Test execution (but not benchmark execution) is parallelized by default and
#   scales with the number of processor cores available. You can change the
#   number of processes by passing the `-jN` flag to `meson test`.
#
# 4. Install the Git distribution. Again, this can be done via Meson, Ninja or
#    Samurai:
#
#      $ meson install
#      $ ninja install
#      $ samu install
#
#    The prefix into which Git shall be installed is defined when setting up
#    the build directory. More on that in the "Configuration" section.
#
# Meson supports multiple backends. The default backend generates Ninja build
# instructions, but it also supports the generation of Microsoft Visual
# Studio solutions as well as Xcode projects by passing the `--backend` option
# to `meson setup`. IDEs like Eclipse and Visual Studio Code provide plugins to
# import Meson files directly.
#
# Configuration
# =============
#
# The exact configuration of Git is determined when setting up the build
# directory via `meson setup`. Unless told otherwise, Meson will automatically
# detect the availability of various bits and pieces. There are two different
# kinds of options that can be used to further tweak the build:
#
#   - Built-in options provided by Meson.
#
#   - Options defined by the project in the "meson_options.txt" file.
#
# Both kinds of options can be inspected by running `meson configure` in the
# build directory, which will give you a list of the current value for all
# options.
#
# Options can be configured either when setting up the build directory or can
# be changed in preexisting build directories:
#
#      # Set up a new build directory with optimized settings that will be
#      # installed into an alternative prefix.
#      $ meson setup --buildtype release --optimization 3 --strip --prefix=/home/$USER build
#
#      # Set up a new build directory with a higher warning level. Level 2 is
#      # mostly equivalent to setting DEVELOPER=1, level 3 and "everything"
#      # will enable even more warnings.
#      $ meson setup -Dwarning_level=2 build
#
#      # Set up a new build directory with 'address' and 'undefined' sanitizers
#      # using Clang.
#      $ CC=clang meson setup -Db_sanitize=address,undefined build
#
#      # Disable tests in a preexisting build directory.
#      $ meson configure -Dtests=false
#
#      # Disable features based on Python
#      $ meson configure -Dpython=disabled
#
# Options have a type like booleans, choices, strings or features. Features are
# somewhat special as they can have one of three values: enabled, disabled or
# auto. While the first two values are self-explanatory, "auto" will enable or
# disable the feature based on the availability of prerequisites to support it.
# Python-based features for example will be enabled automatically when a Python
# interpreter could be found. The default value of such features can be changed
# via `meson setup --auto-features={enabled,disabled,auto}`, which will set the
# value of all features with a value of "auto" to the provided one by default.
#
# It is also possible to store a set of configuration options in machine files.
# This can be useful in case you regularly want to reuse the same set of options:
#
#   [binaries]
#   c = ['clang']
#   ar = ['ar']
#
#   [project options]
#   gettext = 'disabled'
#   default_editor = 'vim'
#
#   [built-in options]
#   b_lto = true
#   b_sanitize = 'address,undefined'
#
# These machine files can be passed to `meson setup` via the `--native-file`
# option.
#
# Cross compilation
# =================
#
# Machine files can also be used in the context of cross-compilation to
# describe the target machine as well as the cross-compiler toolchain that
# shall be used. An example machine file could look like the following:
#
#   [binaries]
#   c = 'x86_64-w64-mingw32-gcc'
#   cpp = 'x86_64-w64-mingw32-g++'
#   ar = 'x86_64-w64-mingw32-ar'
#   windres = 'x86_64-w64-mingw32-windres'
#   strip = 'x86_64-w64-mingw32-strip'
#   exe_wrapper = 'wine64'
#   sh = 'C:/Program Files/Git for Windows/usr/bin/sh.exe'
#
#   [host_machine]
#   system = 'windows'
#   cpu_family = 'x86_64'
#   cpu = 'x86_64'
#   endian = 'little'
#
# These machine files can be passed to `meson setup` via the `--cross-file`
# option.
#
# Note that next to the cross-compiler toolchain, the `[binaries]` section is
# also used to locate a couple of binaries that will be built into Git. This
# includes `sh`, `python` and `perl`, so when cross-compiling Git you likely
# want to set these binary paths in addition to the cross-compiler toolchain
# binaries.
#
# Subproject wrappers
# ===================
#
# Subproject wrappers are a feature provided by Meson that allows the automatic
# fallback to a "wrapped" dependency in case the dependency is not provided by
# the system. For example if the system is lacking curl, then Meson will use
# "subprojects/curl.wrap" to set up curl as a subproject and compile and link
# the dependency into Git itself. This is especially helpful on systems like
# Windows, where you typically don't have such dependencies installed.
#
# The use of subproject wrappers can be disabled by executing `meson setup`
# with the `--wrap-mode nofallback` option.

project('git', 'c',
  meson_version: '>=0.61.0',
  # The version is only of cosmetic nature, so if we cannot find a shell yet we
  # simply don't set up a version at all. This may be the case for example on
  # Windows systems, where we first have to bootstrap the host environment.
  version: find_program('sh', native: true, required: false).found() ? run_command(
    'GIT-VERSION-GEN', meson.current_source_dir(), '--format=@GIT_VERSION@',
    capture: true,
    check: true,
  ).stdout().strip() : 'unknown',
  # Git requires C99 with GNU extensions, which of course isn't supported by
  # MSVC. Funny enough, C99 doesn't work with MSVC either, as it has only
  # learned to define __STDC_VERSION__ with C11 and later. We thus require
  # GNU C99 and fall back to C11. Meson only learned to handle the fallback
  # with version 1.3.0, so on older versions we use GNU C99 unconditionally.
  default_options: meson.version().version_compare('>=1.3.0') ? ['c_std=gnu99,c11'] : ['c_std=gnu99'],
)

fs = import('fs')

program_path = []
if get_option('sane_tool_path').length() != 0
  program_path = get_option('sane_tool_path')
elif host_machine.system() == 'windows'
  # Git for Windows provides all the tools we need to build Git.
  program_path = [ 'C:/Program Files/Git/bin', 'C:/Program Files/Git/usr/bin' ]
endif

cygpath = find_program('cygpath', dirs: program_path, native: true, required: false)
diff = find_program('diff', dirs: program_path, native: true)
git = find_program('git', dirs: program_path, native: true, required: false)
sed = find_program('sed', dirs: program_path, native: true)
shell = find_program('sh', dirs: program_path, native: true)
tar = find_program('tar', dirs: program_path, native: true)
time = find_program('time', dirs: program_path, required: get_option('benchmarks'))

# Detect the target shell that is used by Git at runtime. Note that we prefer
# "/bin/sh" over a PATH-based lookup, which provides a working shell on most
# supported systems. This path is also the default shell path used by our
# Makefile. This lookup can be overridden via `program_path`.
target_shell = find_program('sh', dirs: program_path + [ '/bin' ], native: false)

# Sanity-check that programs required for the build exist.
foreach tool : ['cat', 'cut', 'grep', 'sort', 'tr', 'uname']
  find_program(tool, dirs: program_path, native: true)
endforeach

script_environment = environment()
foreach path : program_path
  script_environment.prepend('PATH', path)
endforeach

# The environment used by GIT-VERSION-GEN. Note that we explicitly override
# environment variables that might be set by the user. This is by design so
# that we always use whatever Meson has configured instead of what is present
# in the environment.
version_gen_environment = script_environment
version_gen_environment.set('GIT_BUILT_FROM_COMMIT', get_option('built_from_commit'))
version_gen_environment.set('GIT_DATE', get_option('build_date'))
version_gen_environment.set('GIT_USER_AGENT', get_option('user_agent'))
version_gen_environment.set('GIT_VERSION', get_option('version'))

compiler = meson.get_compiler('c')

libgit_sources = [
  'abspath.c',
  'add-interactive.c',
  'add-patch.c',
  'advice.c',
  'alias.c',
  'alloc.c',
  'apply.c',
  'archive-tar.c',
  'archive-zip.c',
  'archive.c',
  'attr.c',
  'base85.c',
  'bisect.c',
  'blame.c',
  'blob.c',
  'bloom.c',
  'branch.c',
  'bulk-checkin.c',
  'bundle-uri.c',
  'bundle.c',
  'cache-tree.c',
  'cbtree.c',
  'chdir-notify.c',
  'checkout.c',
  'chunk-format.c',
  'color.c',
  'column.c',
  'combine-diff.c',
  'commit-graph.c',
  'commit-reach.c',
  'commit.c',
  'common-exit.c',
  'common-init.c',
  'compat/nonblock.c',
  'compat/obstack.c',
  'compat/open.c',
  'compat/terminal.c',
  'compiler-tricks/not-constant.c',
  'config.c',
  'connect.c',
  'connected.c',
  'convert.c',
  'copy.c',
  'credential.c',
  'csum-file.c',
  'ctype.c',
  'date.c',
  'decorate.c',
  'delta-islands.c',
  'diagnose.c',
  'diff-delta.c',
  'diff-merges.c',
  'diff-lib.c',
  'diff-no-index.c',
  'diff.c',
  'diffcore-break.c',
  'diffcore-delta.c',
  'diffcore-order.c',
  'diffcore-pickaxe.c',
  'diffcore-rename.c',
  'diffcore-rotate.c',
  'dir-iterator.c',
  'dir.c',
  'editor.c',
  'entry.c',
  'environment.c',
  'ewah/bitmap.c',
  'ewah/ewah_bitmap.c',
  'ewah/ewah_io.c',
  'ewah/ewah_rlw.c',
  'exec-cmd.c',
  'fetch-negotiator.c',
  'fetch-pack.c',
  'fmt-merge-msg.c',
  'fsck.c',
  'fsmonitor.c',
  'fsmonitor-ipc.c',
  'fsmonitor-settings.c',
  'gettext.c',
  'git-zlib.c',
  'gpg-interface.c',
  'graph.c',
  'grep.c',
  'hash-lookup.c',
  'hash.c',
  'hashmap.c',
  'help.c',
  'hex.c',
  'hex-ll.c',
  'hook.c',
  'ident.c',
  'json-writer.c',
  'kwset.c',
  'levenshtein.c',
  'line-log.c',
  'line-range.c',
  'linear-assignment.c',
  'list-objects-filter-options.c',
  'list-objects-filter.c',
  'list-objects.c',
  'lockfile.c',
  'log-tree.c',
  'loose.c',
  'ls-refs.c',
  'mailinfo.c',
  'mailmap.c',
  'match-trees.c',
  'mem-pool.c',
  'merge-blobs.c',
  'merge-ll.c',
  'merge-ort.c',
  'merge-ort-wrappers.c',
  'merge.c',
  'midx.c',
  'midx-write.c',
  'name-hash.c',
  'negotiator/default.c',
  'negotiator/noop.c',
  'negotiator/skipping.c',
  'notes-cache.c',
  'notes-merge.c',
  'notes-utils.c',
  'notes.c',
  'object-file-convert.c',
  'object-file.c',
  'object-name.c',
  'object.c',
  'odb.c',
  'oid-array.c',
  'oidmap.c',
  'oidset.c',
  'oidtree.c',
  'pack-bitmap-write.c',
  'pack-bitmap.c',
  'pack-check.c',
  'pack-mtimes.c',
  'pack-objects.c',
  'pack-revindex.c',
  'pack-write.c',
  'packfile.c',
  'pager.c',
  'parallel-checkout.c',
  'parse.c',
  'parse-options-cb.c',
  'parse-options.c',
  'patch-delta.c',
  'patch-ids.c',
  'path.c',
  'path-walk.c',
  'pathspec.c',
  'pkt-line.c',
  'preload-index.c',
  'pretty.c',
  'prio-queue.c',
  'progress.c',
  'promisor-remote.c',
  'prompt.c',
  'protocol.c',
  'protocol-caps.c',
  'prune-packed.c',
  'pseudo-merge.c',
  'quote.c',
  'range-diff.c',
  'reachable.c',
  'read-cache.c',
  'rebase-interactive.c',
  'rebase.c',
  'ref-filter.c',
  'reflog-walk.c',
  'reflog.c',
  'refs.c',
  'refs/debug.c',
  'refs/files-backend.c',
  'refs/reftable-backend.c',
  'refs/iterator.c',
  'refs/packed-backend.c',
  'refs/ref-cache.c',
  'refspec.c',
  'reftable/basics.c',
  'reftable/error.c',
  'reftable/block.c',
  'reftable/blocksource.c',
  'reftable/iter.c',
  'reftable/merged.c',
  'reftable/pq.c',
  'reftable/record.c',
  'reftable/stack.c',
  'reftable/system.c',
  'reftable/table.c',
  'reftable/tree.c',
  'reftable/writer.c',
  'remote.c',
  'replace-object.c',
  'repo-settings.c',
  'repository.c',
  'rerere.c',
  'reset.c',
  'resolve-undo.c',
  'revision.c',
  'run-command.c',
  'send-pack.c',
  'sequencer.c',
  'serve.c',
  'server-info.c',
  'setup.c',
  'shallow.c',
  'sideband.c',
  'sigchain.c',
  'sparse-index.c',
  'split-index.c',
  'stable-qsort.c',
  'statinfo.c',
  'strbuf.c',
  'streaming.c',
  'string-list.c',
  'strmap.c',
  'strvec.c',
  'sub-process.c',
  'submodule-config.c',
  'submodule.c',
  'symlinks.c',
  'tag.c',
  'tempfile.c',
  'thread-utils.c',
  'tmp-objdir.c',
  'trace.c',
  'trace2.c',
  'trace2/tr2_cfg.c',
  'trace2/tr2_cmd_name.c',
  'trace2/tr2_ctr.c',
  'trace2/tr2_dst.c',
  'trace2/tr2_sid.c',
  'trace2/tr2_sysenv.c',
  'trace2/tr2_tbuf.c',
  'trace2/tr2_tgt_event.c',
  'trace2/tr2_tgt_normal.c',
  'trace2/tr2_tgt_perf.c',
  'trace2/tr2_tls.c',
  'trace2/tr2_tmr.c',
  'trailer.c',
  'transport-helper.c',
  'transport.c',
  'tree-diff.c',
  'tree-walk.c',
  'tree.c',
  'unpack-trees.c',
  'upload-pack.c',
  'url.c',
  'urlmatch.c',
  'usage.c',
  'userdiff.c',
  'utf8.c',
  'varint.c',
  'version.c',
  'versioncmp.c',
  'walker.c',
  'wildmatch.c',
  'worktree.c',
  'wrapper.c',
  'write-or-die.c',
  'ws.c',
  'wt-status.c',
  'xdiff-interface.c',
  'xdiff/xdiffi.c',
  'xdiff/xemit.c',
  'xdiff/xhistogram.c',
  'xdiff/xmerge.c',
  'xdiff/xpatience.c',
  'xdiff/xprepare.c',
  'xdiff/xutils.c',
]

libgit_sources += custom_target(
  input: 'command-list.txt',
  output: 'command-list.h',
  command: [shell, meson.current_source_dir() + '/generate-cmdlist.sh', meson.current_source_dir(), '@OUTPUT@'],
  env: script_environment,
)

builtin_sources = [
  'builtin/add.c',
  'builtin/am.c',
  'builtin/annotate.c',
  'builtin/apply.c',
  'builtin/archive.c',
  'builtin/backfill.c',
  'builtin/bisect.c',
  'builtin/blame.c',
  'builtin/branch.c',
  'builtin/bugreport.c',
  'builtin/bundle.c',
  'builtin/cat-file.c',
  'builtin/check-attr.c',
  'builtin/check-ignore.c',
  'builtin/check-mailmap.c',
  'builtin/check-ref-format.c',
  'builtin/checkout--worker.c',
  'builtin/checkout-index.c',
  'builtin/checkout.c',
  'builtin/clean.c',
  'builtin/clone.c',
  'builtin/column.c',
  'builtin/commit-graph.c',
  'builtin/commit-tree.c',
  'builtin/commit.c',
  'builtin/config.c',
  'builtin/count-objects.c',
  'builtin/credential-cache--daemon.c',
  'builtin/credential-cache.c',
  'builtin/credential-store.c',
  'builtin/credential.c',
  'builtin/describe.c',
  'builtin/diagnose.c',
  'builtin/diff-files.c',
  'builtin/diff-index.c',
  'builtin/diff-pairs.c',
  'builtin/diff-tree.c',
  'builtin/diff.c',
  'builtin/difftool.c',
  'builtin/fast-export.c',
  'builtin/fast-import.c',
  'builtin/fetch-pack.c',
  'builtin/fetch.c',
  'builtin/fmt-merge-msg.c',
  'builtin/for-each-ref.c',
  'builtin/for-each-repo.c',
  'builtin/fsck.c',
  'builtin/fsmonitor--daemon.c',
  'builtin/gc.c',
  'builtin/get-tar-commit-id.c',
  'builtin/grep.c',
  'builtin/hash-object.c',
  'builtin/help.c',
  'builtin/hook.c',
  'builtin/index-pack.c',
  'builtin/init-db.c',
  'builtin/interpret-trailers.c',
  'builtin/log.c',
  'builtin/ls-files.c',
  'builtin/ls-remote.c',
  'builtin/ls-tree.c',
  'builtin/mailinfo.c',
  'builtin/mailsplit.c',
  'builtin/merge-base.c',
  'builtin/merge-file.c',
  'builtin/merge-index.c',
  'builtin/merge-ours.c',
  'builtin/merge-recursive.c',
  'builtin/merge-tree.c',
  'builtin/merge.c',
  'builtin/mktag.c',
  'builtin/mktree.c',
  'builtin/multi-pack-index.c',
  'builtin/mv.c',
  'builtin/name-rev.c',
  'builtin/notes.c',
  'builtin/pack-objects.c',
  'builtin/pack-refs.c',
  'builtin/patch-id.c',
  'builtin/prune-packed.c',
  'builtin/prune.c',
  'builtin/pull.c',
  'builtin/push.c',
  'builtin/range-diff.c',
  'builtin/read-tree.c',
  'builtin/rebase.c',
  'builtin/receive-pack.c',
  'builtin/reflog.c',
  'builtin/refs.c',
  'builtin/remote-ext.c',
  'builtin/remote-fd.c',
  'builtin/remote.c',
  'builtin/repack.c',
  'builtin/replace.c',
  'builtin/replay.c',
  'builtin/rerere.c',
  'builtin/reset.c',
  'builtin/rev-list.c',
  'builtin/rev-parse.c',
  'builtin/revert.c',
  'builtin/rm.c',
  'builtin/send-pack.c',
  'builtin/shortlog.c',
  'builtin/show-branch.c',
  'builtin/show-index.c',
  'builtin/show-ref.c',
  'builtin/sparse-checkout.c',
  'builtin/stash.c',
  'builtin/stripspace.c',
  'builtin/submodule--helper.c',
  'builtin/symbolic-ref.c',
  'builtin/tag.c',
  'builtin/unpack-file.c',
  'builtin/unpack-objects.c',
  'builtin/update-index.c',
  'builtin/update-ref.c',
  'builtin/update-server-info.c',
  'builtin/upload-archive.c',
  'builtin/upload-pack.c',
  'builtin/var.c',
  'builtin/verify-commit.c',
  'builtin/verify-pack.c',
  'builtin/verify-tag.c',
  'builtin/worktree.c',
  'builtin/write-tree.c',
]

third_party_excludes = [
  ':!contrib',
  ':!compat/inet_ntop.c',
  ':!compat/inet_pton.c',
  ':!compat/nedmalloc',
  ':!compat/obstack.*',
  ':!compat/poll',
  ':!compat/regex',
  ':!sha1collisiondetection',
  ':!sha1dc',
  ':!t/unit-tests/clar',
  ':!t/t[0-9][0-9][0-9][0-9]*',
  ':!xdiff',
]

headers_to_check = []
if git.found() and fs.exists(meson.project_source_root() / '.git')
  foreach header : run_command(git, '-C', meson.project_source_root(), 'ls-files', '--deduplicate', '*.h', third_party_excludes, check: true).stdout().split()
    headers_to_check += header
  endforeach
endif

if not get_option('breaking_changes')
  builtin_sources += 'builtin/pack-redundant.c'
endif

builtin_sources += custom_target(
  output: 'config-list.h',
  command: [
    shell,
    meson.current_source_dir() + '/generate-configlist.sh',
    meson.current_source_dir(),
    '@OUTPUT@',
  ],
  env: script_environment,
)

builtin_sources += custom_target(
  input: 'Documentation/githooks.adoc',
  output: 'hook-list.h',
  command: [
    shell,
    meson.current_source_dir() + '/generate-hooklist.sh',
    meson.current_source_dir(),
    '@OUTPUT@',
  ],
  env: script_environment,
)

# This contains the variables for GIT-BUILD-OPTIONS, which we use to propagate
# build options to our tests.
build_options_config = configuration_data()
build_options_config.set('GIT_INTEROP_MAKE_OPTS', '')
build_options_config.set_quoted('GIT_PERF_LARGE_REPO', get_option('benchmark_large_repo'))
build_options_config.set('GIT_PERF_MAKE_COMMAND', '')
build_options_config.set('GIT_PERF_MAKE_OPTS', '')
build_options_config.set_quoted('GIT_PERF_REPEAT_COUNT', get_option('benchmark_repeat_count').to_string())
build_options_config.set_quoted('GIT_PERF_REPO', get_option('benchmark_repo'))
build_options_config.set('GIT_TEST_CMP_USE_COPIED_CONTEXT', '')
build_options_config.set('GIT_TEST_INDEX_VERSION', '')
build_options_config.set('GIT_TEST_OPTS', '')
build_options_config.set('GIT_TEST_PERL_FATAL_WARNINGS', '')
build_options_config.set_quoted('GIT_TEST_UTF8_LOCALE', get_option('test_utf8_locale'))
build_options_config.set_quoted('LOCALEDIR', fs.as_posix(get_option('prefix') / get_option('localedir')))
build_options_config.set_quoted('GITWEBDIR', fs.as_posix(get_option('prefix') / get_option('datadir') / 'gitweb'))

if get_option('sane_tool_path').length() != 0
  sane_tool_path = (host_machine.system() == 'windows' ? ';' : ':').join(get_option('sane_tool_path'))
  build_options_config.set_quoted('BROKEN_PATH_FIX', 's|^\# @BROKEN_PATH_FIX@$|git_broken_path_fix "' + sane_tool_path + '"|')
else
  build_options_config.set_quoted('BROKEN_PATH_FIX', '/^\# @BROKEN_PATH_FIX@$/d')
endif

test_output_directory = get_option('test_output_directory')
if test_output_directory == ''
  test_output_directory = meson.project_build_root() / 'test-output'
endif

# These variables are used for building libgit.a.
libgit_c_args = [
  '-DBINDIR="' + get_option('bindir') + '"',
  '-DDEFAULT_GIT_TEMPLATE_DIR="' + get_option('datadir') / 'git-core/templates' + '"',
  '-DFALLBACK_RUNTIME_PREFIX="' + get_option('prefix') + '"',
  '-DGIT_HOST_CPU="' + host_machine.cpu_family() + '"',
  '-DGIT_HTML_PATH="' + get_option('datadir') / 'doc/git-doc"',
  '-DGIT_INFO_PATH="' + get_option('infodir') + '"',
  '-DGIT_LOCALE_PATH="' + get_option('localedir') + '"',
  '-DGIT_MAN_PATH="' + get_option('mandir') + '"',
  '-DPAGER_ENV="' + get_option('pager_environment') + '"',
  '-DSHELL_PATH="' + fs.as_posix(target_shell.full_path()) + '"',
]

system_attributes = get_option('gitattributes')
if system_attributes != ''
  libgit_c_args += '-DETC_GITATTRIBUTES="' + system_attributes + '"'
else
  libgit_c_args += '-DETC_GITATTRIBUTES="' + get_option('sysconfdir') / 'gitattributes"'
endif

system_config = get_option('gitconfig')
if system_config != ''
  libgit_c_args += '-DETC_GITCONFIG="' + system_config + '"'
else
  libgit_c_args += '-DETC_GITCONFIG="' + get_option('sysconfdir') / 'gitconfig"'
endif

editor_opt = get_option('default_editor')
if editor_opt != '' and editor_opt != 'vi'
  libgit_c_args += '-DDEFAULT_EDITOR="' + editor_opt + '"'
endif

pager_opt = get_option('default_pager')
if pager_opt != '' and pager_opt != 'less'
  libgit_c_args += '-DDEFAULT_PAGER="' + pager_opt + '"'
endif

help_format_opt = get_option('default_help_format')
if help_format_opt == 'platform'
  if host_machine.system() == 'windows'
    help_format_opt = 'html'
  else
    help_format_opt = 'man'
  endif
endif
if help_format_opt != 'man'
    libgit_c_args += '-DDEFAULT_HELP_FORMAT="' + help_format_opt + '"'
endif

libgit_include_directories = [ '.' ]
libgit_dependencies = [ ]

# Treat any warning level above 1 the same as we treat DEVELOPER=1 in our
# Makefile.
if get_option('warning_level') in ['2','3', 'everything'] and compiler.get_argument_syntax() == 'gcc'
  foreach cflag : [
    '-Wcomma',
    '-Wdeclaration-after-statement',
    '-Wformat-security',
    '-Wold-style-definition',
    '-Woverflow',
    '-Wpointer-arith',
    '-Wstrict-prototypes',
    '-Wunreachable-code',
    '-Wunused',
    '-Wvla',
    '-Wwrite-strings',
    '-fno-common',
    '-Wtautological-constant-out-of-range-compare',
    # If a function is public, there should be a prototype and the right
    # header file should be included. If not, it should be static.
    '-Wmissing-prototypes',
    # These are disabled because we have these all over the place.
    '-Wno-empty-body',
    '-Wno-missing-field-initializers',
  ]
    if compiler.has_argument(cflag)
      libgit_c_args += cflag
    endif
  endforeach
endif

if get_option('breaking_changes')
  build_options_config.set('WITH_BREAKING_CHANGES', 'YesPlease')
  libgit_c_args += '-DWITH_BREAKING_CHANGES'
else
  build_options_config.set('WITH_BREAKING_CHANGES', '')
endif

if get_option('b_sanitize').contains('address')
  build_options_config.set('SANITIZE_ADDRESS', 'YesCompiledWithIt')
else
  build_options_config.set('SANITIZE_ADDRESS', '')
endif
if get_option('b_sanitize').contains('leak')
  build_options_config.set('SANITIZE_LEAK', 'YesCompiledWithIt')
else
  build_options_config.set('SANITIZE_LEAK', '')
endif
if get_option('b_sanitize').contains('undefined')
  libgit_c_args += '-DSHA1DC_FORCE_ALIGNED_ACCESS'
endif

executable_suffix = ''
if host_machine.system() == 'cygwin' or host_machine.system() == 'windows'
  executable_suffix = '.exe'
  libgit_c_args += '-DSTRIP_EXTENSION="' + executable_suffix + '"'
endif
build_options_config.set_quoted('X', executable_suffix)

python = import('python').find_installation('python3', required: get_option('python'))
target_python = find_program('python3', native: false, required: python.found())
if python.found()
  build_options_config.set('NO_PYTHON', '')
else
  libgit_c_args += '-DNO_PYTHON'
  build_options_config.set('NO_PYTHON', '1')
endif

# Perl is used for two different things: our test harness and to provide some
# features. It is optional if you want to neither execute tests nor use any of
# these optional features.
perl_required = get_option('perl')
if get_option('benchmarks').enabled() or get_option('gitweb').enabled() or 'netrc' in get_option('credential_helpers')
  perl_required = true
endif

# Note that we only set NO_PERL if the Perl features were disabled by the user.
# It may not be set when we have found Perl, but only use it to run tests.
#
# At the time of writing, executing `perl --version` results in a string
# similar to the following output:
#
#     This is perl 5, version 40, subversion 0 (v5.40.0) built for x86_64-linux-thread-multi
#
# Meson picks up the "40" as version number instead of using "v5.40.0"
# due to the regular expression it uses. This got fixed in Meson 1.7.0,
# but meanwhile we have to either use `-V:version` instead of `--version`,
# which we can do starting with Meson 1.5.0 and newer, or we have to
# match against the minor version.
if meson.version().version_compare('>=1.5.0')
  perl = find_program('perl', dirs: program_path, native: true, required: perl_required, version: '>=5.26.0', version_argument: '-V:version')
  target_perl = find_program('perl', dirs: program_path, native: false, required: perl.found(), version: '>=5.26.0', version_argument: '-V:version')
else
  perl = find_program('perl', dirs: program_path, native: true, required: perl_required, version: '>=26')
  target_perl = find_program('perl', dirs: program_path, native: false, required: perl.found(), version: '>=26')
endif
perl_features_enabled = perl.found() and get_option('perl').allowed()
if perl_features_enabled
  build_options_config.set('NO_PERL', '')

  if get_option('runtime_prefix')
    build_options_config.set('PERL_LOCALEDIR', '')
  else
    build_options_config.set_quoted('PERL_LOCALEDIR', fs.as_posix(get_option('prefix') / get_option('localedir')))
  endif

  if get_option('perl_cpan_fallback')
    build_options_config.set('NO_PERL_CPAN_FALLBACKS', '')
  else
    build_options_config.set_quoted('NO_PERL_CPAN_FALLBACKS', 'YesPlease')
  endif
else
  libgit_c_args += '-DNO_PERL'
  build_options_config.set('NO_PERL', '1')
  build_options_config.set('PERL_LOCALEDIR', '')
  build_options_config.set('NO_PERL_CPAN_FALLBACKS', '')
endif

zlib_backend = get_option('zlib_backend')
if zlib_backend in ['auto', 'zlib-ng']
  zlib_ng = dependency('zlib-ng', required: zlib_backend == 'zlib-ng')
  if zlib_ng.found()
    zlib_backend = 'zlib-ng'
    libgit_c_args += '-DHAVE_ZLIB_NG'
    libgit_dependencies += zlib_ng
  endif
endif
if zlib_backend in ['auto', 'zlib']
  zlib = dependency('zlib', default_options: ['default_library=static', 'tests=disabled'])
  if zlib.version().version_compare('<1.2.0')
    libgit_c_args += '-DNO_DEFLATE_BOUND'
  endif
  zlib_backend = 'zlib'
  libgit_dependencies += zlib
endif

threads = dependency('threads', required: false)
if threads.found()
  libgit_dependencies += threads
  build_options_config.set('NO_PTHREADS', '')
else
  libgit_c_args += '-DNO_PTHREADS'
  build_options_config.set('NO_PTHREADS', '1')
endif

msgfmt = find_program('msgfmt', dirs: program_path, native: true, required: false)
gettext_option = get_option('gettext').disable_auto_if(not msgfmt.found())
if not msgfmt.found() and gettext_option.enabled()
  error('Internationalization via libintl requires msgfmt')
endif

if gettext_option.allowed() and host_machine.system() == 'darwin' and get_option('macos_use_homebrew_gettext')
  if host_machine.cpu_family() == 'x86_64'
    libintl_prefix = '/usr/local'
  elif host_machine.cpu_family() == 'aarch64'
    libintl_prefix = '/opt/homebrew'
  else
    error('Homebrew workaround not supported on current architecture')
  endif

  intl = compiler.find_library('intl', dirs: libintl_prefix / 'lib', required: gettext_option)
  if intl.found()
    intl = declare_dependency(
      dependencies: intl,
      include_directories: libintl_prefix / 'include',
    )
  endif
else
  intl = dependency('intl', required: gettext_option)
endif
if intl.found()
  libgit_dependencies += intl
  build_options_config.set('NO_GETTEXT', '')
  build_options_config.set('USE_GETTEXT_SCHEME', '')

  # POSIX nowadays requires `nl_langinfo()`, but some systems still don't have
  # the function available. On such systems we instead fall back to libcharset.
  # On native Windows systems we use our own emulation.
  if host_machine.system() != 'windows' and not compiler.has_function('nl_langinfo')
    libcharset = compiler.find_library('charset', required: true)
    libgit_dependencies += libcharset
    libgit_c_args += '-DHAVE_LIBCHARSET_H'
  endif
else
  libgit_c_args += '-DNO_GETTEXT'
  build_options_config.set('NO_GETTEXT', '1')
  build_options_config.set('USE_GETTEXT_SCHEME', 'fallthrough')
endif

iconv = dependency('iconv', required: get_option('iconv'))
if iconv.found()
  libgit_dependencies += iconv
  build_options_config.set('NO_ICONV', '')

  have_old_iconv = false
  if not compiler.compiles('''
    #include <iconv.h>

    extern size_t iconv(iconv_t cd,
                        char **inbuf, size_t *inbytesleft,
                        char **outbuf, size_t *outbytesleft);
  ''', name: 'old iconv interface', dependencies: [iconv])
    libgit_c_args += '-DOLD_ICONV'
    have_old_iconv = true
  endif

  iconv_omits_bom_source = '''#
    #include <iconv.h>

    int main(int argc, const char **argv)
    {
  '''
  if have_old_iconv
    iconv_omits_bom_source += '''
      typedef const char *iconv_ibp;
    '''
  else
    iconv_omits_bom_source += '''
      typedef char *iconv_ibp;
    '''
  endif
  iconv_omits_bom_source += '''
      int v;
      iconv_t conv;
      char in[] = "a"; iconv_ibp pin = in;
      char out[20] = ""; char *pout = out;
      size_t isz = sizeof in;
      size_t osz = sizeof out;

      conv = iconv_open("UTF-16", "UTF-8");
      iconv(conv, &pin, &isz, &pout, &osz);
      iconv_close(conv);
      v = (unsigned char)(out[0]) + (unsigned char)(out[1]);
      return v != 0xfe + 0xff;
    }
  '''

  if compiler.run(iconv_omits_bom_source,
    dependencies: iconv,
    name: 'iconv omits BOM',
  ).returncode() != 0
    libgit_c_args += '-DICONV_OMITS_BOM'
  endif
else
  libgit_c_args += '-DNO_ICONV'
  build_options_config.set('NO_ICONV', '1')
endif

pcre2 = dependency('libpcre2-8', required: get_option('pcre2'), default_options: ['default_library=static', 'test=false'])
if pcre2.found()
  libgit_dependencies += pcre2
  libgit_c_args += '-DUSE_LIBPCRE2'
  build_options_config.set('USE_LIBPCRE2', '1')
else
  build_options_config.set('USE_LIBPCRE2', '')
endif

curl = dependency('libcurl', version: '>=7.21.3', required: get_option('curl'), default_options: ['default_library=static', 'tests=disabled', 'tool=disabled'])
use_curl_for_imap_send = false
if curl.found()
  if curl.version().version_compare('>=7.34.0')
    libgit_c_args += '-DUSE_CURL_FOR_IMAP_SEND'
    use_curl_for_imap_send = true
  endif

  # Most executables don't have to link against libcurl, but we still need its
  # include directories so that we can resolve LIBCURL_VERSION in "help.c".
  libgit_dependencies += curl.partial_dependency(includes: true)
  build_options_config.set('NO_CURL', '')
else
  libgit_c_args += '-DNO_CURL'
  build_options_config.set('NO_CURL', '1')
endif

expat = dependency('expat', required: get_option('expat'), default_options: ['default_library=static', 'build_tests=false'])
if expat.found()
  libgit_dependencies += expat

  if expat.version().version_compare('<=1.2')
    libgit_c_args += '-DEXPAT_NEEDS_XMLPARSE_H'
  endif
  build_options_config.set('NO_EXPAT', '')
else
  libgit_c_args += '-DNO_EXPAT'
  build_options_config.set('NO_EXPAT', '1')
endif

if not compiler.has_header('sys/select.h')
  libgit_c_args += '-DNO_SYS_SELECT_H'
endif

has_poll_h = compiler.has_header('poll.h')
if not has_poll_h
  libgit_c_args += '-DNO_POLL_H'
endif

has_sys_poll_h = compiler.has_header('sys/poll.h')
if not has_sys_poll_h
  libgit_c_args += '-DNO_SYS_POLL_H'
endif

if not has_poll_h and not has_sys_poll_h
  libgit_c_args += '-DNO_POLL'
  libgit_sources += 'compat/poll/poll.c'
  libgit_include_directories += 'compat/poll'
endif

if not compiler.has_header('inttypes.h')
  libgit_c_args += '-DNO_INTTYPES_H'
endif

if compiler.has_header('alloca.h')
  libgit_c_args += '-DHAVE_ALLOCA_H'
endif

# Windows has libgen.h and a basename implementation, but we still need our own
# implementation to threat things like drive prefixes specially.
if host_machine.system() == 'windows' or not compiler.has_header('libgen.h')
  libgit_c_args += '-DNO_LIBGEN_H'
  libgit_sources += 'compat/basename.c'
endif

if compiler.has_header('paths.h')
  libgit_c_args += '-DHAVE_PATHS_H'
endif

if compiler.has_header('strings.h')
  libgit_c_args += '-DHAVE_STRINGS_H'
endif

networking_dependencies = [ ]
if host_machine.system() == 'windows'
  winsock = compiler.find_library('ws2_32', required: false)
  if winsock.found()
    networking_dependencies += winsock
  endif
else
  networking_dependencies += [
    compiler.find_library('nsl', required: false),
    compiler.find_library('resolv', required: false),
    compiler.find_library('socket', required: false),
  ]
endif
libgit_dependencies += networking_dependencies

if host_machine.system() != 'windows'
  foreach symbol : ['inet_ntop', 'inet_pton', 'hstrerror']
    if not compiler.has_function(symbol, dependencies: networking_dependencies)
      libgit_c_args += '-DNO_' + symbol.to_upper()
      libgit_sources += 'compat/' + symbol + '.c'
    endif
  endforeach
endif

has_ipv6 = compiler.has_function('getaddrinfo', dependencies: networking_dependencies)
if not has_ipv6
  libgit_c_args += '-DNO_IPV6'
endif

if not compiler.compiles('''
  #ifdef _WIN32
  # include <winsock2.h>
  #else
  # include <sys/types.h>
  # include <sys/socket.h>
  #endif

  void func(void)
  {
    struct sockaddr_storage x;
  }
''', name: 'struct sockaddr_storage')
  if has_ipv6
    libgit_c_args += '-Dsockaddr_storage=sockaddr_in6'
  else
    libgit_c_args += '-Dsockaddr_storage=sockaddr_in'
  endif
endif

if compiler.has_function('socket', dependencies: networking_dependencies)
  libgit_sources += [
    'unix-socket.c',
    'unix-stream-server.c',
  ]
  build_options_config.set('NO_UNIX_SOCKETS', '')
else
  libgit_c_args += '-DNO_UNIX_SOCKETS'
  build_options_config.set('NO_UNIX_SOCKETS', '1')
endif

if host_machine.system() == 'darwin'
  libgit_sources += 'compat/precompose_utf8.c'
  libgit_c_args += '-DPRECOMPOSE_UNICODE'
  libgit_c_args += '-DPROTECT_HFS_DEFAULT'
endif

# Configure general compatibility wrappers.
if host_machine.system() == 'cygwin'
  libgit_sources += [
    'compat/win32/path-utils.c',
  ]
elif host_machine.system() == 'windows'
  libgit_sources += [
    'compat/winansi.c',
    'compat/win32/dirent.c',
    'compat/win32/flush.c',
    'compat/win32/path-utils.c',
    'compat/win32/pthread.c',
    'compat/win32/syslog.c',
    'compat/win32mmap.c',
    'compat/nedmalloc/nedmalloc.c',
  ]

  libgit_c_args += [
    '-DDETECT_MSYS_TTY',
    '-DENSURE_MSYSTEM_IS_SET',
    '-DNATIVE_CRLF',
    '-DNOGDI',
    '-DNO_POSIX_GOODIES',
    '-DWIN32',
    '-D_CONSOLE',
    '-D_CONSOLE_DETECT_MSYS_TTY',
    '-D__USE_MINGW_ANSI_STDIO=0',
  ]

  libgit_dependencies += compiler.find_library('ntdll')
  libgit_include_directories += 'compat/win32'
  if compiler.get_id() == 'msvc'
    libgit_include_directories += 'compat/vcbuild/include'
    libgit_sources += 'compat/msvc.c'
  else
    libgit_sources += 'compat/mingw.c'
  endif
endif

if host_machine.system() == 'linux'
  libgit_sources += 'compat/linux/procinfo.c'
elif host_machine.system() == 'windows'
  libgit_sources += 'compat/win32/trace2_win32_process_info.c'
else
  libgit_sources += 'compat/stub/procinfo.c'
endif

if host_machine.system() == 'cygwin' or host_machine.system() == 'windows'
  libgit_c_args += [
    '-DUNRELIABLE_FSTAT',
    '-DMMAP_PREVENTS_DELETE',
    '-DOBJECT_CREATION_MODE=1',
  ]
endif

# Configure the simple-ipc subsystem required fro the fsmonitor.
if host_machine.system() == 'windows'
  libgit_sources += [
    'compat/simple-ipc/ipc-shared.c',
    'compat/simple-ipc/ipc-win32.c',
  ]
  libgit_c_args += '-DSUPPORTS_SIMPLE_IPC'
else
  libgit_sources += [
    'compat/simple-ipc/ipc-shared.c',
    'compat/simple-ipc/ipc-unix-socket.c',
  ]
  libgit_c_args += '-DSUPPORTS_SIMPLE_IPC'
endif

fsmonitor_backend = ''
if host_machine.system() == 'windows'
  fsmonitor_backend = 'win32'
elif host_machine.system() == 'darwin'
  fsmonitor_backend = 'darwin'
  libgit_dependencies += dependency('CoreServices')
endif
if fsmonitor_backend != ''
  libgit_c_args += '-DHAVE_FSMONITOR_DAEMON_BACKEND'
  libgit_c_args += '-DHAVE_FSMONITOR_OS_SETTINGS'

  libgit_sources += [
    'compat/fsmonitor/fsm-health-' + fsmonitor_backend + '.c',
    'compat/fsmonitor/fsm-ipc-' + fsmonitor_backend + '.c',
    'compat/fsmonitor/fsm-listen-' + fsmonitor_backend + '.c',
    'compat/fsmonitor/fsm-path-utils-' + fsmonitor_backend + '.c',
    'compat/fsmonitor/fsm-settings-' + fsmonitor_backend + '.c',
  ]
endif
build_options_config.set_quoted('FSMONITOR_DAEMON_BACKEND', fsmonitor_backend)
build_options_config.set_quoted('FSMONITOR_OS_SETTINGS', fsmonitor_backend)

if not get_option('b_sanitize').contains('address') and get_option('regex').allowed() and compiler.has_header('regex.h') and compiler.get_define('REG_STARTEND', prefix: '#include <regex.h>') != ''
  build_options_config.set('NO_REGEX', '')

  if compiler.get_define('REG_ENHANCED', prefix: '#include <regex.h>') != ''
    libgit_c_args += '-DUSE_ENHANCED_BASIC_REGULAR_EXPRESSIONS'
    libgit_sources += 'compat/regcomp_enhanced.c'
  endif
elif not get_option('regex').enabled()
  libgit_c_args += [
    '-DNO_REGEX',
    '-DGAWK',
    '-DNO_MBSUPPORT',
  ]
  build_options_config.set('NO_REGEX', '1')
  libgit_sources += 'compat/regex/regex.c'
  libgit_include_directories += 'compat/regex'
else
    error('Native regex support requested but not found')
endif

# setitimer and friends are provided by compat/mingw.c.
if host_machine.system() != 'windows'
  if not compiler.compiles('''
    #include <sys/time.h>
    void func(void)
    {
      struct itimerval value;
    }
  ''', name: 'struct itimerval')
    libgit_c_args += '-DNO_STRUCT_ITIMERVAL'
    libgit_c_args += '-DNO_SETITIMER'
  elif not compiler.has_function('setitimer')
    libgit_c_args += '-DNO_SETITIMER'
  endif
endif

if compiler.has_member('struct sysinfo', 'totalram', prefix: '#include <sys/sysinfo.h>')
  libgit_c_args += '-DHAVE_SYSINFO'
endif

if compiler.has_member('struct stat', 'st_mtimespec.tv_nsec', prefix: '#include <sys/stat.h>')
  libgit_c_args += '-DUSE_ST_TIMESPEC'
elif not compiler.has_member('struct stat', 'st_mtim.tv_nsec', prefix: '#include <sys/stat.h>')
  libgit_c_args += '-DNO_NSEC'
endif

if not compiler.has_member('struct stat', 'st_blocks', prefix: '#include <sys/stat.h>')
  libgit_c_args += '-DNO_ST_BLOCKS_IN_STRUCT_STAT'
endif

if not compiler.has_member('struct dirent', 'd_type', prefix: '#include <dirent.h>')
  libgit_c_args += '-DNO_D_TYPE_IN_DIRENT'
endif

if not compiler.has_member('struct passwd', 'pw_gecos', prefix: '#include <pwd.h>')
  libgit_c_args += '-DNO_GECOS_IN_PWENT'
endif

checkfuncs = {
  'strcasestr' : ['strcasestr.c'],
  'memmem' : ['memmem.c'],
  'strlcpy' : ['strlcpy.c'],
  'strtoull' : [],
  'setenv' : ['setenv.c'],
  'mkdtemp' : ['mkdtemp.c'],
  'initgroups' : [],
  'strtoumax' : ['strtoumax.c', 'strtoimax.c'],
  'pread' : ['pread.c'],
}

if host_machine.system() == 'windows'
  libgit_c_args += '-DUSE_WIN32_MMAP'
else
  checkfuncs += {
    'mmap' : ['mmap.c'],
    # provided by compat/mingw.c.
    'unsetenv' : ['unsetenv.c'],
    # provided by compat/mingw.c.
    'getpagesize' : [],
  }
endif

foreach func, impls : checkfuncs
  if not compiler.has_function(func)
    libgit_c_args += '-DNO_' + func.to_upper()
    foreach impl : impls
      libgit_sources += 'compat/' + impl
    endforeach
  endif
endforeach

if compiler.has_function('sync_file_range')
  libgit_c_args += '-DHAVE_SYNC_FILE_RANGE'
endif

if not compiler.has_function('strdup')
  libgit_c_args += '-DOVERRIDE_STRDUP'
  libgit_sources += 'compat/strdup.c'
endif

if not compiler.has_function('qsort')
  libgit_c_args += '-DINTERNAL_QSORT'
endif
libgit_sources += 'compat/qsort_s.c'

if compiler.has_function('getdelim')
  libgit_c_args += '-DHAVE_GETDELIM'
endif


if compiler.has_function('clock_gettime')
  libgit_c_args += '-DHAVE_CLOCK_GETTIME'
endif

if compiler.compiles('''
  #include <time.h>

  void func(void)
  {
    clockid_t id = CLOCK_MONOTONIC;
  }
''', name: 'monotonic clock')
  libgit_c_args += '-DHAVE_CLOCK_MONOTONIC'
endif

if not compiler.compiles('''
  #include <inttypes.h>

  void func(void)
  {
    uintmax_t x = 0;
  }
''', name: 'uintmax_t')
  libgit_c_args += '-DNO_UINTMAX_T'
endif

has_bsd_sysctl = false
if compiler.has_header('sys/sysctl.h')
  if compiler.compiles('''
    #include <stddef.h>
    #include <sys/sysctl.h>

    void func(void)
    {
      int val, mib[2] = { 0 };
      size_t len = sizeof(val);
      sysctl(mib, 2, &val, &len, NULL, 0);
    }
  ''', name: 'BSD sysctl')
    libgit_c_args += '-DHAVE_BSD_SYSCTL'
    has_bsd_sysctl = true
  endif
endif

if not meson.is_cross_build() and compiler.run('''
  #include <stdio.h>

  int main(int argc, const char **argv)
  {
    FILE *f = fopen(".", "r");
    return f ? 0 : 1;
  }
''', name: 'fread reads directories').returncode() == 0
  libgit_c_args += '-DFREAD_READS_DIRECTORIES'
  libgit_sources += 'compat/fopen.c'
endif

if not meson.is_cross_build() and fs.exists('/dev/tty')
  libgit_c_args += '-DHAVE_DEV_TTY'
endif

csprng_backend = get_option('csprng_backend')
https_backend = get_option('https_backend')
sha1_backend = get_option('sha1_backend')
sha1_unsafe_backend = get_option('sha1_unsafe_backend')
sha256_backend = get_option('sha256_backend')

security_framework = dependency('Security', required: 'CommonCrypto' in [https_backend, sha1_backend, sha1_unsafe_backend])
core_foundation_framework = dependency('CoreFoundation', required: security_framework.found())
if https_backend == 'auto' and security_framework.found()
  https_backend = 'CommonCrypto'
endif

openssl_required = 'openssl' in [csprng_backend, https_backend, sha1_backend, sha1_unsafe_backend, sha256_backend]
openssl = dependency('openssl',
  required: openssl_required,
  allow_fallback: openssl_required or https_backend == 'auto',
  default_options: ['default_library=static'],
)
if https_backend == 'auto' and openssl.found()
  https_backend = 'openssl'
endif

if https_backend == 'CommonCrypto'
  libgit_dependencies += security_framework
  libgit_dependencies += core_foundation_framework
  libgit_c_args += '-DAPPLE_COMMON_CRYPTO'
elif https_backend == 'openssl'
  libgit_dependencies += openssl
else
  # We either couldn't find any dependencies with 'auto' or the user requested
  # 'none'. Both cases are benign.
  https_backend = 'none'
endif

if https_backend != 'openssl'
  libgit_c_args += '-DNO_OPENSSL'
endif

if sha1_backend == 'sha1dc'
  libgit_c_args += '-DSHA1_DC'
  libgit_c_args += '-DSHA1DC_NO_STANDARD_INCLUDES=1'
  libgit_c_args += '-DSHA1DC_INIT_SAFE_HASH_DEFAULT=0'
  libgit_c_args += '-DSHA1DC_CUSTOM_INCLUDE_SHA1_C="git-compat-util.h"'
  libgit_c_args += '-DSHA1DC_CUSTOM_INCLUDE_UBC_CHECK_C="git-compat-util.h"'

  libgit_sources += [
    'sha1dc_git.c',
    'sha1dc/sha1.c',
    'sha1dc/ubc_check.c',
  ]
endif
if sha1_backend == 'CommonCrypto' or sha1_unsafe_backend == 'CommonCrypto'
  if sha1_backend == 'CommonCrypto'
    libgit_c_args += '-DSHA1_APPLE'
  endif
  if sha1_unsafe_backend == 'CommonCrypto'
    libgit_c_args += '-DSHA1_APPLE_UNSAFE'
  endif

  libgit_c_args += '-DCOMMON_DIGEST_FOR_OPENSSL'
  # Apple CommonCrypto requires chunking
  libgit_c_args += '-DSHA1_MAX_BLOCK_SIZE=1024L*1024L*1024L'
endif
if sha1_backend == 'openssl' or sha1_unsafe_backend == 'openssl'
  if sha1_backend == 'openssl'
    libgit_c_args += '-DSHA1_OPENSSL'
  endif
  if sha1_unsafe_backend == 'openssl'
    libgit_c_args += '-DSHA1_OPENSSL_UNSAFE'
  endif

  libgit_dependencies += openssl
endif
if sha1_backend == 'block' or sha1_unsafe_backend == 'block'
  if sha1_backend == 'block'
    libgit_c_args += '-DSHA1_BLK'
  endif
  if sha1_unsafe_backend == 'block'
    libgit_c_args += '-DSHA1_BLK_UNSAFE'
  endif

  libgit_sources += 'block-sha1/sha1.c'
endif

if sha256_backend == 'openssl'
  libgit_c_args += '-DSHA256_OPENSSL'
  libgit_dependencies += openssl
elif sha256_backend == 'nettle'
  nettle = dependency('nettle')
  libgit_dependencies += nettle
  libgit_c_args += '-DSHA256_NETTLE'
elif sha256_backend == 'gcrypt'
  gcrypt = dependency('gcrypt')
  libgit_dependencies += gcrypt
  libgit_c_args += '-DSHA256_GCRYPT'
elif sha256_backend == 'block'
  libgit_c_args += '-DSHA256_BLK'
  libgit_sources += 'sha256/block/sha256.c'
else
  error('Unhandled SHA256 backend ' + sha256_backend)
endif

# Backends are ordered to reflect our preference for more secure and faster
# ones over the ones that are less so.
if csprng_backend in ['auto', 'arc4random'] and compiler.has_header_symbol('stdlib.h', 'arc4random_buf', required: csprng_backend == 'arc4random')
  libgit_c_args += '-DHAVE_ARC4RANDOM'
  csprng_backend = 'arc4random'
elif csprng_backend in ['auto', 'arc4random_bsd'] and compiler.has_header_symbol('bsd/stdlib.h', 'arc4random_buf', required: csprng_backend == 'arc4random_bsd')
  libgit_c_args += '-DHAVE_ARC4RANDOM_BSD'
  csprng_backend = 'arc4random_bsd'
elif csprng_backend in ['auto', 'getrandom'] and compiler.has_header_symbol('sys/random.h', 'getrandom', required: csprng_backend == 'getrandom')
  libgit_c_args += '-DHAVE_GETRANDOM'
  csprng_backend = 'getrandom'
elif csprng_backend in ['auto', 'getentropy'] and compiler.has_header_symbol('unistd.h', 'getentropy', required: csprng_backend == 'getentropy')
  libgit_c_args += '-DHAVE_GETENTROPY'
  csprng_backend = 'getentropy'
elif csprng_backend in ['auto', 'rtlgenrandom'] and compiler.has_header_symbol('ntsecapi.h', 'RtlGenRandom', prefix: '#include <windows.h>', required: csprng_backend == 'rtlgenrandom')
  libgit_c_args += '-DHAVE_RTLGENRANDOM'
  csprng_backend = 'rtlgenrandom'
elif csprng_backend in ['auto', 'openssl'] and openssl.found()
  libgit_c_args += '-DHAVE_OPENSSL_CSPRNG'
  csprng_backend = 'openssl'
elif csprng_backend in ['auto', 'urandom']
  csprng_backend = 'urandom'
else
  error('Unsupported CSPRNG backend: ' + csprng_backend)
endif

git_exec_path = 'libexec/git-core'
libexec = get_option('libexecdir')
if libexec != 'libexec' and libexec != '.'
  git_exec_path = libexec
endif

if get_option('runtime_prefix')
  libgit_c_args += '-DRUNTIME_PREFIX'
  build_options_config.set('RUNTIME_PREFIX', 'true')

  if git_exec_path.startswith('/')
    error('runtime_prefix requires a relative libexecdir not:', libexec)
  endif

  if compiler.has_header('mach-o/dyld.h')
    libgit_c_args += '-DHAVE_NS_GET_EXECUTABLE_PATH'
  endif

  if has_bsd_sysctl and compiler.compiles('''
    #include <sys/sysctl.h>

    void func(void)
    {
      KERN_PROC_PATHNAME; KERN_PROC;
    }
  ''', name: 'BSD KERN_PROC_PATHNAME')
    libgit_c_args += '-DHAVE_NS_GET_EXECUTABLE_PATH'
  endif

  if host_machine.system() == 'linux'
    libgit_c_args += '-DPROCFS_EXECUTABLE_PATH="/proc/self/exe' + '"'
  elif host_machine.system() == 'openbsd'
    libgit_c_args += '-DPROCFS_EXECUTABLE_PATH="' + '/proc/curproc/file' + '"'
  elif host_machine.system() == 'netbsd'
    libgit_c_args += '-DPROCFS_EXECUTABLE_PATH="' + '/proc/curproc/exe' + '"'
  endif

  if host_machine.system() == 'windows' and compiler.compiles('''
    #include <stdlib.h>

    void func(void)
    {
      _wpgmptr;
    }
  ''', name: 'Win32 _wpgmptr')
    libgit_c_args += '-DHAVE_WPGMPTR'
  endif
else
  build_options_config.set('RUNTIME_PREFIX', 'false')
endif
libgit_c_args += '-DGIT_EXEC_PATH="' + git_exec_path + '"'

git_version_file = custom_target(
  command: [
    shell,
    meson.current_source_dir() / 'GIT-VERSION-GEN',
    meson.current_source_dir(),
    '@INPUT@',
    '@OUTPUT@',
  ],
  input: meson.current_source_dir() / 'GIT-VERSION-FILE.in',
  output: 'GIT-VERSION-FILE',
  env: version_gen_environment,
  build_always_stale: true,
)

version_def_h = custom_target(
  command: [
    shell,
    meson.current_source_dir() / 'GIT-VERSION-GEN',
    meson.current_source_dir(),
    '@INPUT@',
    '@OUTPUT@',
  ],
  input: meson.current_source_dir() / 'version-def.h.in',
  output: 'version-def.h',
  # Depend on GIT-VERSION-FILE so that we don't always try to rebuild this
  # target for the same commit.
  depends: [git_version_file],
  env: version_gen_environment,
)
libgit_sources += version_def_h

libgit = declare_dependency(
  link_with: static_library('git',
    sources: libgit_sources,
    c_args: libgit_c_args + [
      '-DGIT_VERSION_H="' + version_def_h.full_path() + '"',
    ],
    dependencies: libgit_dependencies,
    include_directories: libgit_include_directories,
  ),
  compile_args: libgit_c_args,
  dependencies: libgit_dependencies,
  include_directories: libgit_include_directories,
)

common_main_sources = ['common-main.c']
common_main_link_args = [ ]
if host_machine.system() == 'windows'
  git_rc = custom_target(
    command: [
      shell,
      meson.current_source_dir() / 'GIT-VERSION-GEN',
      meson.current_source_dir(),
      '@INPUT@',
      '@OUTPUT@',
    ],
    input: meson.current_source_dir() / 'git.rc.in',
    output: 'git.rc',
    depends: [git_version_file],
    env: version_gen_environment,
  )

  common_main_sources += import('windows').compile_resources(git_rc,
    include_directories: [meson.current_source_dir()],
  )
  if compiler.get_argument_syntax() == 'gcc'
    common_main_link_args += [
      '-municode',
      '-Wl,-nxcompat',
      '-Wl,-dynamicbase',
      '-Wl,-pic-executable,-e,mainCRTStartup',
    ]
  elif compiler.get_argument_syntax() == 'msvc'
    common_main_link_args += [
      '/ENTRY:wmainCRTStartup',
      'invalidcontinue.obj',
    ]
  else
    error('Unsupported compiler ' + compiler.get_id())
  endif
endif

libgit_commonmain = declare_dependency(
  link_with: static_library('common-main',
    sources: common_main_sources,
    dependencies: [ libgit ],
  ),
  link_args: common_main_link_args,
  dependencies: [ libgit ],
)

bin_wrappers = [ ]
test_dependencies = [ ]

git_builtin = executable('git',
  sources: builtin_sources + 'git.c',
  dependencies: [libgit_commonmain],
  install: true,
  install_dir: get_option('libexecdir') / 'git-core',
)
bin_wrappers += git_builtin

test_dependencies += executable('git-daemon',
  sources: 'daemon.c',
  dependencies: [libgit_commonmain],
  install: true,
  install_dir: get_option('libexecdir') / 'git-core',
)

test_dependencies += executable('git-sh-i18n--envsubst',
  sources: 'sh-i18n--envsubst.c',
  dependencies: [libgit_commonmain],
  install: true,
  install_dir: get_option('libexecdir') / 'git-core',
)

bin_wrappers += executable('git-shell',
  sources: 'shell.c',
  dependencies: [libgit_commonmain],
  install: true,
  install_dir: get_option('libexecdir') / 'git-core',
)

test_dependencies += executable('git-http-backend',
  sources: 'http-backend.c',
  dependencies: [libgit_commonmain],
  install: true,
  install_dir: get_option('libexecdir') / 'git-core',
)

bin_wrappers += executable('scalar',
  sources: 'scalar.c',
  dependencies: [libgit_commonmain],
  install: true,
  install_dir: get_option('libexecdir') / 'git-core',
)

if curl.found()
  libgit_curl = declare_dependency(
    sources: [
      'http.c',
      'http-walker.c',
    ],
    dependencies: [libgit_commonmain, curl],
  )

  test_dependencies += executable('git-remote-http',
    sources: 'remote-curl.c',
    dependencies: [libgit_curl],
    install: true,
    install_dir: get_option('libexecdir') / 'git-core',
  )

  test_dependencies += executable('git-http-fetch',
    sources: 'http-fetch.c',
    dependencies: [libgit_curl],
    install: true,
    install_dir: get_option('libexecdir') / 'git-core',
  )

  if expat.found()
    test_dependencies += executable('git-http-push',
      sources: 'http-push.c',
      dependencies: [libgit_curl],
      install: true,
      install_dir: get_option('libexecdir') / 'git-core',
    )
  endif

  foreach alias : [ 'git-remote-https', 'git-remote-ftp', 'git-remote-ftps' ]
    test_dependencies += executable(alias,
      sources: 'remote-curl.c',
      dependencies: [libgit_curl],
    )

    install_symlink(alias + executable_suffix,
      install_dir: get_option('libexecdir') / 'git-core',
      pointing_to: 'git-remote-http',
    )
  endforeach
endif

test_dependencies += executable('git-imap-send',
  sources: 'imap-send.c',
  dependencies: [ use_curl_for_imap_send ? libgit_curl : libgit_commonmain ],
  install: true,
  install_dir: get_option('libexecdir') / 'git-core',
)

foreach alias : [ 'git-receive-pack', 'git-upload-archive', 'git-upload-pack' ]
  bin_wrappers += executable(alias,
    objects: git_builtin.extract_all_objects(recursive: false),
    dependencies: [libgit_commonmain],
  )

  install_symlink(alias + executable_suffix,
    install_dir: get_option('libexecdir') / 'git-core',
    pointing_to: 'git',
  )
endforeach

foreach symlink : [
  'git',
  'git-receive-pack',
  'git-shell',
  'git-upload-archive',
  'git-upload-pack',
  'scalar',
]
  if meson.version().version_compare('>=1.3.0')
    pointing_to = fs.relative_to(get_option('libexecdir') / 'git-core' / symlink, get_option('bindir'))
  else
    pointing_to = '../libexec/git-core' / symlink
  endif

  install_symlink(symlink,
    install_dir: get_option('bindir'),
    pointing_to: pointing_to,
  )
endforeach

scripts_sh = [
  'git-difftool--helper.sh',
  'git-filter-branch.sh',
  'git-merge-octopus.sh',
  'git-merge-one-file.sh',
  'git-merge-resolve.sh',
  'git-mergetool--lib.sh',
  'git-mergetool.sh',
  'git-quiltimport.sh',
  'git-request-pull.sh',
  'git-sh-i18n.sh',
  'git-sh-setup.sh',
  'git-submodule.sh',
  'git-web--browse.sh',
]
if perl_features_enabled
  scripts_sh += 'git-instaweb.sh'
endif

foreach script : scripts_sh
  test_dependencies += custom_target(
    input: script,
    output: fs.stem(script),
    command: [
      shell,
      meson.project_source_root() / 'generate-script.sh',
      '@INPUT@',
      '@OUTPUT@',
      meson.project_build_root() / 'GIT-BUILD-OPTIONS',
    ],
    install: true,
    install_dir: get_option('libexecdir') / 'git-core',
  )
endforeach

if perl_features_enabled
  scripts_perl = [
    'git-archimport.perl',
    'git-cvsexportcommit.perl',
    'git-cvsimport.perl',
    'git-cvsserver.perl',
    'git-send-email.perl',
    'git-svn.perl',
  ]

  pathsep = ':'
  if host_machine.system() == 'windows'
    pathsep = ';'
  endif

  perl_header_template = 'perl/header_templates/fixed_prefix.template.pl'
  if get_option('runtime_prefix')
    perl_header_template = 'perl/header_templates/runtime_prefix.template.pl'
  endif

  perllibdir = get_option('perllibdir')
  if perllibdir == ''
    perllibdir = get_option('datadir') / 'perl5'
  endif

  perl_header = configure_file(
    input: perl_header_template,
    output: 'GIT-PERL-HEADER',
    configuration: {
      'GITEXECDIR_REL': get_option('libexecdir') / 'git-core',
      'PERLLIBDIR_REL': perllibdir,
      'LOCALEDIR_REL': get_option('datadir') / 'locale',
      'INSTLIBDIR': perllibdir,
      'PATHSEP': pathsep,
    },
  )

  generate_perl_command = [
    shell,
    meson.project_source_root() / 'generate-perl.sh',
    meson.project_build_root() / 'GIT-BUILD-OPTIONS',
    git_version_file.full_path(),
    perl_header,
    '@INPUT@',
    '@OUTPUT@',
  ]

  foreach script : scripts_perl
    generated_script = custom_target(
      input: script,
      output: fs.stem(script),
      command: generate_perl_command,
      install: true,
      install_dir: get_option('libexecdir') / 'git-core',
      depends: [git_version_file],
    )
    test_dependencies += generated_script

    if script == 'git-cvsserver.perl'
      bin_wrappers += generated_script

      if meson.version().version_compare('>=1.3.0')
        pointing_to = fs.relative_to(get_option('libexecdir') / 'git-core' / fs.stem(script), get_option('bindir'))
      else
        pointing_to = '../libexec/git-core' / fs.stem(script)
      endif

      install_symlink(fs.stem(script),
        install_dir: get_option('bindir'),
        pointing_to: pointing_to,
      )
    endif
  endforeach

  subdir('perl')
endif

if python.found()
  scripts_python = [
    'git-p4.py'
  ]

  foreach script : scripts_python
    generated_python = custom_target(
      input: script,
      output: fs.stem(script),
      command: [
        shell,
        meson.project_source_root() / 'generate-python.sh',
        meson.project_build_root() / 'GIT-BUILD-OPTIONS',
        '@INPUT@',
        '@OUTPUT@',
      ],
      install: true,
      install_dir: get_option('libexecdir') / 'git-core',
    )
    test_dependencies += generated_python
  endforeach
endif

mergetools = [
  'mergetools/araxis',
  'mergetools/bc',
  'mergetools/codecompare',
  'mergetools/deltawalker',
  'mergetools/diffmerge',
  'mergetools/diffuse',
  'mergetools/ecmerge',
  'mergetools/emerge',
  'mergetools/examdiff',
  'mergetools/guiffy',
  'mergetools/gvimdiff',
  'mergetools/kdiff3',
  'mergetools/kompare',
  'mergetools/meld',
  'mergetools/nvimdiff',
  'mergetools/opendiff',
  'mergetools/p4merge',
  'mergetools/smerge',
  'mergetools/tkdiff',
  'mergetools/tortoisemerge',
  'mergetools/vimdiff',
  'mergetools/vscode',
  'mergetools/winmerge',
  'mergetools/xxdiff',
]

foreach mergetool : mergetools
  install_data(mergetool, install_dir: get_option('libexecdir') / 'git-core' / 'mergetools')
endforeach

if intl.found()
  subdir('po')
endif

# Gitweb requires Perl, so we disable the auto-feature if Perl was not found.
# We make sure further up that Perl is required in case the gitweb option is
# enabled.
gitweb_option = get_option('gitweb').disable_auto_if(not perl.found())
if gitweb_option.allowed()
  subdir('gitweb')
  build_options_config.set('NO_GITWEB', '')
else
  build_options_config.set('NO_GITWEB', '1')
endif

subdir('templates')

# Everything but the bin-wrappers need to come before this target such that we
# can properly set up test dependencies. The bin-wrappers themselves are set up
# at configuration time, so these are fine.
if get_option('tests')
  test_kwargs = {
    'timeout': 0,
  }

  # The TAP protocol was already understood by previous versions of Meson, but
  # it was incompatible with the `meson test --interactive` flag.
  if meson.version().version_compare('>=1.8.0')
    test_kwargs += {
      'protocol': 'tap',
    }
  endif

  subdir('t')
endif

if get_option('fuzzers')
  subdir('oss-fuzz')
endif

subdir('bin-wrappers')
if get_option('docs') != []
  subdir('Documentation')
endif

subdir('contrib')

exclude_from_check_headers = [
  'compat/',
  'unicode-width.h',
]

if sha1_backend != 'openssl'
  exclude_from_check_headers += 'sha1/openssl.h'
endif
if sha256_backend != 'openssl'
  exclude_from_check_headers += 'sha256/openssl.h'
endif
if sha256_backend != 'nettle'
  exclude_from_check_headers += 'sha256/nettle.h'
endif
if sha256_backend != 'gcrypt'
  exclude_from_check_headers += 'sha256/gcrypt.h'
endif

if headers_to_check.length() != 0 and compiler.get_argument_syntax() == 'gcc'
  hco_targets = []
  foreach h : headers_to_check
    skip_header = false
    foreach exclude : exclude_from_check_headers
      if h.startswith(exclude)
        skip_header = true
        break
      endif
    endforeach

    if skip_header
      continue
    endif

    hcc = custom_target(
      input: h,
      output: h.underscorify() + 'cc',
      command: [
        shell,
        '-c',
        'echo \'#include "git-compat-util.h"\' > @OUTPUT@ && echo \'#include "' + h + '"\' >> @OUTPUT@'
      ]
    )

    hco = custom_target(
      input: hcc,
      output: fs.replace_suffix(h.underscorify(), '.hco'),
      command: [
        compiler.cmd_array(),
        libgit_c_args,
        '-I', meson.project_source_root(),
        '-I', meson.project_source_root() / 't/unit-tests',
        '-o', '/dev/null',
        '-c', '-xc',
        '@INPUT@'
      ]
    )
    hco_targets += hco
  endforeach

  # TODO: deprecate 'hdr-check' in lieu of 'check-headers' in Git 2.51+
  hdr_check = alias_target('hdr-check', hco_targets)
  alias_target('check-headers', hdr_check)
endif

foreach key, value : {
  'DIFF': diff.full_path(),
  'GIT_SOURCE_DIR': meson.project_source_root(),
  'GIT_TEST_CMP': diff.full_path() + ' -u',
  'GIT_TEST_GITPERLLIB': meson.project_build_root() / 'perl',
  'GIT_TEST_TEMPLATE_DIR': meson.project_build_root() / 'templates',
  'GIT_TEST_TEXTDOMAINDIR': meson.project_build_root() / 'po',
  'PAGER_ENV': get_option('pager_environment'),
  'PERL_PATH': target_perl.found() ? target_perl.full_path() : '',
  'PYTHON_PATH': target_python.found () ? target_python.full_path() : '',
  'SHELL_PATH': target_shell.full_path(),
  'TAR': tar.full_path(),
  'TEST_OUTPUT_DIRECTORY': test_output_directory,
  'TEST_SHELL_PATH': shell.full_path(),
}
  if value != '' and cygpath.found()
    value = run_command(cygpath, value, check: true).stdout().strip()
  endif
  build_options_config.set_quoted(key, value)
endforeach

configure_file(
  input: 'GIT-BUILD-OPTIONS.in',
  output: 'GIT-BUILD-OPTIONS',
  configuration: build_options_config,
)

# Development environments can be used via `meson devenv -C <builddir>`. This
# allows you to execute test scripts directly with the built Git version and
# puts the built version of Git in your PATH.
devenv = environment()
devenv.set('GIT_BUILD_DIR', meson.current_build_dir())
devenv.prepend('PATH', meson.current_build_dir() / 'bin-wrappers')
meson.add_devenv(devenv)

# Generate the 'version' file in the distribution tarball. This is used via
# `meson dist -C <builddir>` to populate the source archive with the Git
# version that the archive is being generated from.
meson.add_dist_script(
  shell,
  '-c',
  '"$1" "$2" "$3" --format="@GIT_VERSION@" "$MESON_DIST_ROOT/version"',
  'GIT-VERSION-GEN',
  shell,
  meson.current_source_dir() / 'GIT-VERSION-GEN',
  meson.current_source_dir(),
)

summary({
  'benchmarks': get_option('tests') and perl.found() and time.found(),
  'curl': curl.found(),
  'expat': expat.found(),
  'gettext': intl.found(),
  'gitweb': gitweb_option.allowed(),
  'https': https_backend,
  'iconv': iconv.found(),
  'pcre2': pcre2.found(),
  'perl': perl_features_enabled,
  'python': python.found(),
}, section: 'Auto-detected features')

summary({
  'csprng': csprng_backend,
  'https': https_backend,
  'sha1': sha1_backend,
  'sha1_unsafe': sha1_unsafe_backend,
  'sha256': sha256_backend,
  'zlib': zlib_backend,
}, section: 'Backends')

summary({
  'perl': target_perl,
  'python': target_python,
  'shell': target_shell,
}, section: 'Runtime executable paths')
