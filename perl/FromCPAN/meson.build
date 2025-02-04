test_dependencies += custom_target(
  input: 'Error.pm',
  output: 'Error.pm',
  command: generate_perl_command,
  install: true,
  install_dir: get_option('datadir') / 'perl5/FromCPAN',
  depends: [git_version_file],
)

subdir('Mail')
