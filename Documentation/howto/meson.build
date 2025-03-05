howto_sources = [
  'coordinate-embargoed-releases.adoc',
  'keep-canonical-history-correct.adoc',
  'maintain-git.adoc',
  'new-command.adoc',
  'rebase-from-internal-branch.adoc',
  'rebuild-from-update-hook.adoc',
  'recover-corrupted-blob-object.adoc',
  'recover-corrupted-object-harder.adoc',
  'revert-a-faulty-merge.adoc',
  'revert-branch-rebase.adoc',
  'separating-topic-branches.adoc',
  'setup-git-server-over-http.adoc',
  'update-hook-example.adoc',
  'use-git-daemon.adoc',
  'using-merge-subtree.adoc',
  'using-signed-tag-in-pull-request.adoc',
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
  output: 'howto-index.adoc',
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
      sed,
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
