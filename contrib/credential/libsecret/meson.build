executable('git-credential-libsecret',
  sources: 'git-credential-libsecret.c',
  dependencies: [
    dependency('glib-2.0'),
    dependency('libsecret-1'),
  ],
  install: true,
  install_dir: get_option('libexecdir') / 'git-core',
)
