project('git-gui',
  meson_version: '>=0.61.0',
)

fs = import('fs')

shell = find_program('sh')
tclsh = find_program('tclsh')
wish = find_program('wish')

build_options_config = configuration_data()
if target_machine.system() == 'windows'
  build_options_config.set('GITGUI_RELATIVE', '1')
else
  build_options_config.set('GITGUI_RELATIVE', '')
endif
build_options_config.set_quoted('GITGUI_GITEXECDIR', get_option('prefix') / get_option('libexecdir') / 'git-core')
build_options_config.set_quoted('GITGUI_LIBDIR', get_option('prefix') / get_option('datadir') / 'git-gui/lib')
build_options_config.set_quoted('SHELL_PATH', fs.as_posix(shell.full_path()))
build_options_config.set_quoted('TCLTK_PATH', fs.as_posix(wish.full_path()))
build_options_config.set_quoted('TCL_PATH', fs.as_posix(tclsh.full_path()))
if target_machine.system() == 'darwin'
  tkexecutables = [
    '/Library/Frameworks/Tk.framework/Resources/Wish.app/Contents/MacOS/Wish',
    '/System/Library/Frameworks/Tk.framework/Resources/Wish.app/Contents/MacOS/Wish',
    '/System/Library/Frameworks/Tk.framework/Resources/Wish Shell.app/Contents/MacOS/Wish Shell',
  ]
  tkexecutable = find_program(tkexecutables)
  build_options_config.set_quoted('TKEXECUTABLE', tkexecutable.full_path())
else
  build_options_config.set('TKEXECUTABLE', '')
endif

build_options = configure_file(
  input: 'GIT-GUI-BUILD-OPTIONS.in',
  output: 'GIT-GUI-BUILD-OPTIONS',
  configuration: build_options_config,
)

version_file = custom_target(
  input: 'GIT-VERSION-GEN',
  output: 'GIT-VERSION-FILE',
  command: [
    shell,
    '@INPUT@',
    meson.current_source_dir(),
    '@OUTPUT@',
  ],
  build_always_stale: true,
)

configure_file(
  input: 'git-gui--askpass',
  output: 'git-gui--askpass',
  copy: true,
  install: true,
  install_dir: get_option('libexecdir') / 'git-core',
)

gitgui_main = 'git-gui'
gitgui_main_install_dir = get_option('libexecdir') / 'git-core'

if target_machine.system() == 'windows'
  gitgui_main = 'git-gui.tcl'

  configure_file(
    input: 'windows/git-gui.sh',
    output: 'git-gui',
    copy: true,
    install: true,
    install_dir: get_option('libexecdir') / 'git-core',
  )
elif target_machine.system() == 'darwin'
  gitgui_main = 'git-gui.tcl'
  gitgui_main_install_dir = get_option('datadir') / 'git-gui/lib'

  custom_target(
    output: 'git-gui',
    command: [
      shell,
      meson.current_source_dir() / 'generate-macos-wrapper.sh',
      '@OUTPUT@',
      meson.current_build_dir() / 'GIT-GUI-BUILD-OPTIONS',
      meson.current_build_dir() / 'GIT-VERSION-FILE',
    ],
    depends: [
      version_file,
    ],
    depend_files: [
      build_options,
    ],
    install: true,
    install_dir: get_option('libexecdir') / 'git-core',
  )

  custom_target(
    output: 'Git Gui.app',
    command: [
      shell,
      meson.current_source_dir() / 'generate-macos-app.sh',
      meson.current_source_dir(),
      meson.current_build_dir() / 'Git Gui.app',
      meson.current_build_dir() / 'GIT-GUI-BUILD-OPTIONS',
      meson.current_build_dir() / 'GIT-VERSION-FILE',
    ],
    depends: [
      version_file,
    ],
    depend_files: [
      build_options,
      'macosx/AppMain.tcl',
      'macosx/Info.plist',
      'macosx/git-gui.icns',
    ],
    build_by_default: true,
    install: true,
    install_dir: get_option('datadir') / 'git-gui/lib',
  )
endif

custom_target(
  input: 'git-gui.sh',
  output: gitgui_main,
  command: [
    shell,
    meson.current_source_dir() / 'generate-git-gui.sh',
    '@INPUT@',
    '@OUTPUT@',
    meson.current_build_dir() / 'GIT-GUI-BUILD-OPTIONS',
    meson.current_build_dir() / 'GIT-VERSION-FILE',
  ],
  depends: [
    version_file,
  ],
  depend_files: [
    build_options,
  ],
  install: true,
  install_dir: gitgui_main_install_dir,
)

install_symlink('git-citool',
  install_dir: get_option('libexecdir') / 'git-core',
  pointing_to: 'git-gui',
)

subdir('lib')
subdir('po')
