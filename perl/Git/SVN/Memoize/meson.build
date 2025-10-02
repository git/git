test_dependencies += custom_target(
  input: 'YAML.pm',
  output: 'YAML.pm',
  command: generate_perl_command,
  install: true,
  install_dir: perllibdir / 'Git/SVN/Memoize',
  depends: [git_version_file],
)
