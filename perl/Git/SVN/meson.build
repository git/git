foreach source : [
  'Editor.pm',
  'Fetcher.pm',
  'GlobSpec.pm',
  'Log.pm',
  'Migration.pm',
  'Prompt.pm',
  'Ra.pm',
  'Utils.pm',
]
  test_dependencies += custom_target(
    input: source,
    output: source,
    command: generate_perl_command,
    install: true,
    install_dir: get_option('datadir') / 'perl5/Git/SVN',
    depends: [git_version_file],
  )
endforeach

subdir('Memoize')
