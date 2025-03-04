custom_target(
  input: 'git-contacts',
  output: 'git-contacts',
  command: generate_perl_command,
  depends: [git_version_file],
  install: true,
  install_dir: get_option('libexecdir') / 'git-core',
)

if get_option('docs').contains('man')
  contacts_xml = custom_target(
    command: asciidoc_common_options + [
      '--backend=' + asciidoc_docbook,
      '--doctype=manpage',
      '--out-file=@OUTPUT@',
      '@INPUT@',
    ],
    depends: documentation_deps,
    input: 'git-contacts.adoc',
    output: 'git-contacts.xml',
  )

  custom_target(
    command: [
      xmlto,
      '-m', '@INPUT@',
      'man',
      contacts_xml,
      '-o',
      meson.current_build_dir(),
    ] + xmlto_extra,
    input: [
      '../../Documentation/manpage-normal.xsl',
    ],
    output: 'git-contacts.1',
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
    input: 'git-contacts.adoc',
    output: 'git-contacts.html',
    install: true,
    install_dir: get_option('datadir') / 'doc/git-doc',
  )
endif
