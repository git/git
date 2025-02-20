project('gitk')

shell = find_program('sh')
wish = find_program('wish')

# Verify that dependencies of "generate-tcl.sh" are satisfied.
foreach dependency : [ 'chmod', 'mv', 'sed' ]
  find_program(dependency)
endforeach

custom_target(
  command: [
    shell,
    meson.current_source_dir() / 'generate-tcl.sh',
    wish.full_path(),
    '@INPUT@',
    '@OUTPUT@',
  ],
  input: 'gitk',
  output: 'gitk',
  depend_files: [
    'generate-tcl.sh',
  ],
  install: true,
  install_dir: get_option('bindir'),
)

if find_program('msgfmt').found()
  subdir('po')
endif
