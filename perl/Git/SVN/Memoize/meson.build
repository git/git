test_dependencies += custom_target(
  input: 'YAML.pm',
  output: 'YAML.pm',
  command: generate_perl_command,
  install: true,
  install_dir: get_option('datadir') / 'perl5/Git/SVN',
  depends: [git_version_file],
)
