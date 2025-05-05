coccinelle_opt = get_option('coccinelle').require(
  fs.exists(meson.project_source_root() / '.git'),
  error_message: 'coccinelle can only be run from a git checkout',
)

spatch = find_program('spatch', required: coccinelle_opt)
if not spatch.found()
  subdir_done()
endif

rules = [
  'array.cocci',
  'commit.cocci',
  'config_fn_ctx.pending.cocci',
  'equals-null.cocci',
  'flex_alloc.cocci',
  'free.cocci',
  'git_config_number.cocci',
  'hashmap.cocci',
  'index-compatibility.cocci',
  'object_id.cocci',
  'preincr.cocci',
  'qsort.cocci',
  'refs.cocci',
  'strbuf.cocci',
  'swap.cocci',
  'the_repository.cocci',
  'xcalloc.cocci',
  'xopen.cocci',
  'xstrdup_or_null.cocci',
  'xstrncmpz.cocci',
]

concatenated_rules = custom_target(
  command: [
    'cat', '@INPUT@',
  ],
  input: rules,
  output: 'rules.cocci',
  capture: true,
)

coccinelle_sources = []
foreach source : run_command(git, '-C', meson.project_source_root(), 'ls-files', '--deduplicate', '*.c', third_party_excludes, check: true).stdout().split()
  coccinelle_sources += source
endforeach

coccinelle_headers = []
foreach header : headers_to_check
  coccinelle_headers += meson.project_source_root() / header
endforeach

patches = [ ]
foreach source : coccinelle_sources
  patches += custom_target(
    command: [
      spatch,
      '--all-includes',
      '--sp-file', concatenated_rules,
      '--patch', meson.project_source_root(),
      '@INPUT@',
    ],
    input: meson.project_source_root() / source,
    output: source.underscorify() + '.patch',
    capture: true,
    depend_files: coccinelle_headers,
  )
endforeach

concatenated_patch = custom_target(
  command: [
    'cat', '@INPUT@',
  ],
  input: patches,
  output: 'cocci.patch',
  capture: true,
)

alias_target('coccicheck', concatenated_patch)
