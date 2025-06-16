test_dependencies += custom_target(
  input: 'Error.pm',
  output: 'Error.pm',
  command: generate_perl_command,
  install: true,
  install_dir: perllibdir / 'Git/LoadCPAN',
  depends: [git_version_file],
)

subdir('Mail')
