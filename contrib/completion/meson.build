foreach script : [
  'git-completion.bash',
  'git-completion.tcsh',
  'git-completion.zsh',
  'git-prompt.sh'
]
  if meson.version().version_compare('>=1.3.0')
    test_dependencies += fs.copyfile(script)
  else
    configure_file(
      input: script,
      output: script,
      copy: true,
    )
  endif
endforeach

# We have to discern between the test dependency and the installed file. Our
# tests assume the completion scripts to have the same name as the in-tree
# files, but the installed filenames need to match the executable's basename.
if meson.version().version_compare('>=1.3.0')
  fs.copyfile('git-completion.bash', 'git',
    install: true,
    install_dir: get_option('datadir') / 'bash-completion/completions',
  )
else
  configure_file(
    input: 'git-completion.bash',
    output: 'git',
    copy: true,
    install: true,
    install_dir: get_option('datadir') / 'bash-completion/completions',
  )
endif
