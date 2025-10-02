languages = [
  'bg',
  'de',
  'el',
  'fr',
  'hu',
  'it',
  'ja',
  'nb',
  'pt_br',
  'pt_pt',
  'ru',
  'sv',
  'vi',
  'zh_cn',
]

msgfmt = find_program('msgfmt', required: false)
if not msgfmt.found()
  subdir_done()
endif

foreach language : languages
  custom_target(
    input: language + '.po',
    output: language + '.msg',
    command: [
      msgfmt,
      '--statistics',
      '--tcl',
      '--locale=' + language,
      '-d', meson.current_build_dir(),
      '@INPUT@',
    ],
    install: true,
    install_dir: get_option('datadir') / 'git-gui/lib/msgs',
  )
endforeach
