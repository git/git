template_config = configuration_data()
template_config.set('PERL_PATH', perl.found() ? fs.as_posix(perl.full_path()) : '')
template_config.set('SHELL_PATH', fs.as_posix(shell.full_path()))
template_config.set('GITWEBDIR', fs.as_posix(get_option('prefix') / get_option('datadir') / 'gitweb'))

configure_file(
  input: 'description',
  output: 'description',
  configuration: template_config,
  install: true,
  install_dir: get_option('datadir') / 'git-core/templates',
)

subdir('hooks')
subdir('info')
