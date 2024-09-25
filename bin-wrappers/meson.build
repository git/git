bin_wrappers_config = configuration_data()
foreach key, value : {
      'BUILD_DIR': meson.project_build_root(),
      'MERGE_TOOLS_DIR': meson.project_source_root() / 'mergetools',
      'TEMPLATE_DIR': meson.project_build_root() / 'templates',
      'GIT_TEXTDOMAINDIR': meson.project_build_root() / 'po',
      'GITPERLLIB': meson.project_build_root() / 'perl/lib',
}
  # Paths need to be Unix-style without drive prefixes as they get added to the
  # PATH variable. And given that drive prefixes contain a colon we'd otherwise
  # end up with a broken PATH if we didn't convert them.
  if cygpath.found()
    value = run_command(cygpath, value, check: true).stdout().strip()
  endif
  bin_wrappers_config.set(key, value)
endforeach

foreach executable : bin_wrappers
  executable_config = configuration_data()
  executable_config.merge_from(bin_wrappers_config)
  executable_config.set('PROG', executable.full_path())

  configure_file(
    input: 'wrap-for-bin.sh',
    output: fs.stem(executable.full_path()),
    configuration: executable_config,
  )
endforeach
