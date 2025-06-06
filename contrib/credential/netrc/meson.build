credential_netrc = custom_target(
  input: 'git-credential-netrc.perl',
  output: 'git-credential-netrc',
  command: generate_perl_command,
  depends: [git_version_file],
  install: true,
  install_dir: get_option('libexecdir') / 'git-core',
)

if get_option('tests')
  credential_netrc_testenv = test_environment
  credential_netrc_testenv.set('CREDENTIAL_NETRC_PATH', credential_netrc.full_path())

  test('t-git-credential-netrc',
    shell,
    args: [ meson.current_source_dir() / 't-git-credential-netrc.sh' ],
    workdir: meson.current_source_dir(),
    env: credential_netrc_testenv,
    depends: test_dependencies + bin_wrappers + [credential_netrc],
    kwargs: test_kwargs,
  )
endif
