foreach script : [
  'git-completion.bash',
  'git-completion.tcsh',
  'git-completion.zsh',
  'git-prompt.sh'
]
  # Note that we intentionally don't use `fs.copyfile()` here because we'd have
  # to add it to our test dependencies in that case, but that creates a
  # chicken-and-egg situation between including "t/" or "contrib/" first.
  configure_file(
    input: script,
    output: script,
    copy: true,
  )
endforeach

# We have to discern between the test dependency and the installed file. Our
# tests assume the completion scripts to have the same name as the in-tree
# files, but the installed filenames need to match the executable's basename.
configure_file(
  input: 'git-completion.bash',
  output: 'git',
  copy: true,
  install: true,
  install_dir: get_option('datadir') / 'bash-completion/completions',
)
