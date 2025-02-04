foreach source : [
  'I18N.pm',
  'IndexInfo.pm',
  'LoadCPAN.pm',
  'Packet.pm',
  'SVN.pm',
]
  test_dependencies += custom_target(
    input: source,
    output: source,
    command: generate_perl_command,
    install: true,
    install_dir: get_option('datadir') / 'perl5/Git',
    depends: [git_version_file],
  )
endforeach

subdir('LoadCPAN')
subdir('SVN')
