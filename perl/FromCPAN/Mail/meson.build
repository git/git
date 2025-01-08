test_dependencies += custom_target(
  input: 'Address.pm',
  output: 'Address.pm',
  command: generate_perl_command,
  install: true,
  install_dir: get_option('datadir') / 'perl5/FromCPAN/Mail',
  depends: [git_version_file],
)
