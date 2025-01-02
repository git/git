api_docs = [
  'api-error-handling.txt',
  'api-merge.txt',
  'api-parse-options.txt',
  'api-simple-ipc.txt',
  'api-trace2.txt',
]

articles = [
  'bitmap-format.txt',
  'build-systems.txt',
  'bundle-uri.txt',
  'commit-graph.txt',
  'directory-rename-detection.txt',
  'hash-function-transition.txt',
  'long-running-process-protocol.txt',
  'multi-pack-index.txt',
  'packfile-uri.txt',
  'pack-heuristics.txt',
  'parallel-checkout.txt',
  'partial-clone.txt',
  'platform-support.txt',
  'racy-git.txt',
  'reftable.txt',
  'remembering-renames.txt',
  'repository-version.txt',
  'rerere.txt',
  'scalar.txt',
  'send-pack-pipeline.txt',
  'shallow.txt',
  'sparse-checkout.txt',
  'sparse-index.txt',
  'trivial-merge.txt',
  'unit-tests.txt',
]

api_index = custom_target(
  command: [
    shell,
    meson.current_source_dir() / 'api-index.sh',
    meson.current_source_dir(),
    '@OUTPUT@',
  ],
  env: script_environment,
  input: api_docs,
  output: 'api-index.txt',
)

custom_target(
  command: asciidoc_html_options,
  input: api_index,
  output: 'api-index.html',
  depends: documentation_deps,
  install: true,
  install_dir: get_option('datadir') / 'doc/git-doc/technical',
)

foreach article : api_docs + articles
  custom_target(
    command: asciidoc_html_options,
    input: article,
    output: fs.stem(article) + '.html',
    install: true,
    install_dir: get_option('datadir') / 'doc/git-doc/technical',
  )
endforeach
