executable('git-credential-osxkeychain',
  sources: 'git-credential-osxkeychain.c',
  dependencies: [
    dependency('CoreFoundation'),
    dependency('Security'),
  ],
  install: true,
  install_dir: get_option('libexecdir') / 'git-core',
)
