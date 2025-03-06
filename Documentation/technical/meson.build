api_docs = [
  'api-error-handling.adoc',
  'api-merge.adoc',
  'api-parse-options.adoc',
  'api-simple-ipc.adoc',
  'api-trace2.adoc',
]

articles = [
  'bitmap-format.adoc',
  'build-systems.adoc',
  'bundle-uri.adoc',
  'commit-graph.adoc',
  'directory-rename-detection.adoc',
  'hash-function-transition.adoc',
  'long-running-process-protocol.adoc',
  'multi-pack-index.adoc',
  'packfile-uri.adoc',
  'pack-heuristics.adoc',
  'parallel-checkout.adoc',
  'partial-clone.adoc',
  'platform-support.adoc',
  'racy-git.adoc',
  'reftable.adoc',
  'remembering-renames.adoc',
  'repository-version.adoc',
  'rerere.adoc',
  'scalar.adoc',
  'send-pack-pipeline.adoc',
  'shallow.adoc',
  'sparse-checkout.adoc',
  'sparse-index.adoc',
  'trivial-merge.adoc',
  'unit-tests.adoc',
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
  output: 'api-index.adoc',
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
    depends: documentation_deps,
    install: true,
    install_dir: get_option('datadir') / 'doc/git-doc/technical',
  )
endforeach
