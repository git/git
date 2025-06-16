test_dependencies += custom_target(
  input: 'Git.pm',
  output: 'Git.pm',
  command: generate_perl_command,
  install: true,
  install_dir: perllibdir,
  depends: [git_version_file],
)

subdir('Git')
if get_option('perl_cpan_fallback')
  subdir('FromCPAN')
endif
