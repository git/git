spatch = find_program('spatch', required: get_option('coccinelle'))
if not spatch.found()
  subdir_done()
endif

third_party_sources = [
  ':!contrib',
  ':!compat/inet_ntop.c',
  ':!compat/inet_pton.c',
  ':!compat/nedmalloc',
  ':!compat/obstack.*',
  ':!compat/poll',
  ':!compat/regex',
  ':!sha1collisiondetection',
  ':!sha1dc',
  ':!t/unit-tests/clar',
  ':!t/unit-tests/clar',
  ':!t/t[0-9][0-9][0-9][0-9]*',
]

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

sources = [ ]
foreach source : run_command(git, '-C', meson.project_source_root(), 'ls-files', '--deduplicate', '*.c', third_party_sources, check: true).stdout().split()
  sources += source
endforeach

headers = [ ]
foreach header : run_command(git, '-C', meson.project_source_root(), 'ls-files', '--deduplicate', '*.h', third_party_sources, check: true).stdout().split()
  headers += meson.project_source_root() / header
endforeach

patches = [ ]
foreach source : sources
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
    depend_files: headers,
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
