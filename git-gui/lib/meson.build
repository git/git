libfiles = [
  'about.tcl',
  'blame.tcl',
  'branch_checkout.tcl',
  'branch_create.tcl',
  'branch_delete.tcl',
  'branch_rename.tcl',
  'branch.tcl',
  'browser.tcl',
  'checkout_op.tcl',
  'choose_font.tcl',
  'choose_repository.tcl',
  'choose_rev.tcl',
  'chord.tcl',
  'class.tcl',
  'commit.tcl',
  'console.tcl',
  'database.tcl',
  'date.tcl',
  'diff.tcl',
  'encoding.tcl',
  'error.tcl',
  'index.tcl',
  'line.tcl',
  'logo.tcl',
  'merge.tcl',
  'mergetool.tcl',
  'option.tcl',
  'remote_add.tcl',
  'remote_branch_delete.tcl',
  'remote.tcl',
  'search.tcl',
  'shortcut.tcl',
  'spellcheck.tcl',
  'sshkey.tcl',
  'status_bar.tcl',
  'themed.tcl',
  'tools_dlg.tcl',
  'tools.tcl',
  'transport.tcl',
  'win32.tcl',
]

nontcl_libfiles = [
  'git-gui.ico',
  'win32_shortcut.js',
]

foreach file : libfiles + nontcl_libfiles
  configure_file(
    input: file,
    output: file,
    copy: true,
    install: true,
    install_dir: get_option('datadir') / 'git-gui/lib',
  )
endforeach

custom_target(
  output: 'tclIndex',
  command: [
    shell,
    meson.project_source_root() / 'generate-tclindex.sh',
    meson.project_build_root(),
    meson.project_build_root() / 'GIT-GUI-BUILD-OPTIONS',
    libfiles,
  ],
  depend_files: [
    libfiles,
    build_options,
  ],
  install: true,
  install_dir: get_option('datadir') / 'git-gui/lib',
)
