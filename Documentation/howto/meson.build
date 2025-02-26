howto_sources = [
  'coordinate-embargoed-releases.txt',
  'keep-canonical-history-correct.txt',
  'maintain-git.txt',
  'new-command.txt',
  'rebase-from-internal-branch.txt',
  'rebuild-from-update-hook.txt',
  'recover-corrupted-blob-object.txt',
  'recover-corrupted-object-harder.txt',
  'revert-a-faulty-merge.txt',
  'revert-branch-rebase.txt',
  'separating-topic-branches.txt',
  'setup-git-server-over-http.txt',
  'update-hook-example.txt',
  'use-git-daemon.txt',
  'using-merge-subtree.txt',
  'using-signed-tag-in-pull-request.txt',
]

howto_index = custom_target(
  command: [
    shell,
    meson.current_source_dir() / 'howto-index.sh',
    '@INPUT@',
  ],
  env: script_environment,
  capture: true,
  input: howto_sources,
  output: 'howto-index.txt',
)

custom_target(
  command: asciidoc_html_options,
  input: howto_index,
  output: 'howto-index.html',
  depends: documentation_deps,
  install: true,
  install_dir: get_option('datadir') / 'doc/git-doc',
)

foreach howto : howto_sources
  howto_stripped = custom_target(
    command: [
      find_program('sed'),
      '-e',
      '1,/^$/d',
      '@INPUT@',
    ],
    input: howto,
    output: fs.stem(howto) + '.stripped',
    capture: true,
  )

  custom_target(
    command: asciidoc_html_options,
    input: howto_stripped,
    output: fs.stem(howto_stripped.full_path()) + '.html',
    depends: documentation_deps,
    install: true,
    install_dir: get_option('datadir') / 'doc/git-doc/howto',
  )
endforeach
