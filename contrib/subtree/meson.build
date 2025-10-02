git_subtree = custom_target(
  input: 'git-subtree.sh',
  output: 'git-subtree',
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

if get_option('tests')
  subtree_test_environment = test_environment
  subtree_test_environment.prepend('PATH', meson.current_build_dir())

  test('t7900-subtree', shell,
    args: [ 't7900-subtree.sh' ],
    env: subtree_test_environment,
    workdir: meson.current_source_dir() / 't',
    depends: test_dependencies + bin_wrappers + [ git_subtree ],
    kwargs: test_kwargs,
  )
endif

if get_option('docs').contains('man')
  subtree_xml = custom_target(
    command: asciidoc_common_options + [
      '--backend=' + asciidoc_docbook,
      '--doctype=manpage',
      '--out-file=@OUTPUT@',
      '@INPUT@',
    ],
    depends: documentation_deps,
    input: 'git-subtree.adoc',
    output: 'git-subtree.xml',
  )

  custom_target(
    command: [
      xmlto,
      '-m', '@INPUT@',
      'man',
      subtree_xml,
      '-o',
      meson.current_build_dir(),
    ] + xmlto_extra,
    input: [
      '../../Documentation/manpage-normal.xsl',
    ],
    output: 'git-subtree.1',
    install: true,
    install_dir: get_option('mandir') / 'man1',
  )
endif

if get_option('docs').contains('html')
  custom_target(
    command: asciidoc_common_options + [
      '--backend=' + asciidoc_html,
      '--doctype=manpage',
      '--out-file=@OUTPUT@',
      '@INPUT@',
    ],
    depends: documentation_deps,
    input: 'git-subtree.adoc',
    output: 'git-subtree.html',
    install: true,
    install_dir: get_option('datadir') / 'doc/git-doc',
  )
endif
