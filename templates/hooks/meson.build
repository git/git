hooks = [
  'applypatch-msg.sample',
  'commit-msg.sample',
  'fsmonitor-watchman.sample',
  'post-update.sample',
  'pre-applypatch.sample',
  'pre-commit.sample',
  'pre-merge-commit.sample',
  'prepare-commit-msg.sample',
  'pre-push.sample',
  'pre-rebase.sample',
  'pre-receive.sample',
  'push-to-checkout.sample',
  'sendemail-validate.sample',
  'update.sample',
]

foreach hook : hooks
  configure_file(
    input: hook,
    output: hook,
    configuration: template_config,
    install: true,
    install_dir: get_option('datadir') / 'git-core/templates/hooks',
  )
endforeach
