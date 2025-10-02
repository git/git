test_dependencies += custom_target(
  input: 'Address.pm',
  output: 'Address.pm',
  command: generate_perl_command,
  install: true,
  install_dir: perllibdir / 'Git/LoadCPAN/Mail',
  depends: [git_version_file],
)
