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
