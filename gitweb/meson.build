gitweb_config = configuration_data()
gitweb_config.set_quoted('PERL_PATH', perl.full_path())
gitweb_config.set_quoted('CSSMIN', '')
gitweb_config.set_quoted('JSMIN', '')
gitweb_config.set_quoted('GIT_BINDIR', get_option('prefix') / get_option('bindir'))
gitweb_config.set_quoted('GITWEB_CONFIG', get_option('gitweb_config'))
gitweb_config.set_quoted('GITWEB_CONFIG_SYSTEM', get_option('gitweb_config_system'))
gitweb_config.set_quoted('GITWEB_CONFIG_COMMON', get_option('gitweb_config_common'))
gitweb_config.set_quoted('GITWEB_HOME_LINK_STR', get_option('gitweb_home_link_str'))
gitweb_config.set_quoted('GITWEB_SITENAME', get_option('gitweb_sitename'))
gitweb_config.set_quoted('GITWEB_PROJECTROOT', get_option('gitweb_projectroot'))
gitweb_config.set_quoted('GITWEB_PROJECT_MAXDEPTH', get_option('gitweb_project_maxdepth'))
gitweb_config.set_quoted('GITWEB_EXPORT_OK', get_option('gitweb_export_ok'))
gitweb_config.set_quoted('GITWEB_STRICT_EXPORT', get_option('gitweb_strict_export'))
gitweb_config.set_quoted('GITWEB_BASE_URL', get_option('gitweb_base_url'))
gitweb_config.set_quoted('GITWEB_LIST', get_option('gitweb_list'))
gitweb_config.set_quoted('GITWEB_HOMETEXT', get_option('gitweb_hometext'))
gitweb_config.set_quoted('GITWEB_CSS', get_option('gitweb_css'))
gitweb_config.set_quoted('GITWEB_LOGO', get_option('gitweb_logo'))
gitweb_config.set_quoted('GITWEB_FAVICON', get_option('gitweb_favicon'))
gitweb_config.set_quoted('GITWEB_JS', get_option('gitweb_js'))
gitweb_config.set_quoted('GITWEB_SITE_HTML_HEAD_STRING', get_option('gitweb_site_html_head_string'))
gitweb_config.set_quoted('GITWEB_SITE_HEADER', get_option('gitweb_site_header'))
gitweb_config.set_quoted('GITWEB_SITE_FOOTER', get_option('gitweb_site_footer'))
gitweb_config.set_quoted('HIGHLIGHT_BIN', get_option('highlight_bin'))

configure_file(
  input: 'GITWEB-BUILD-OPTIONS.in',
  output: 'GITWEB-BUILD-OPTIONS',
  configuration: gitweb_config,
)

test_dependencies += custom_target(
  input: 'gitweb.perl',
  output: 'gitweb.cgi',
  command: [
    shell,
    meson.current_source_dir() / 'generate-gitweb-cgi.sh',
    meson.current_build_dir() / 'GITWEB-BUILD-OPTIONS',
    git_version_file.full_path(),
    '@INPUT@',
    '@OUTPUT@',
  ],
  install: true,
  install_dir: get_option('datadir') / 'gitweb',
  depends: [git_version_file],
)

javascript_files = [
  meson.current_source_dir() / 'static/js/adjust-timezone.js',
  meson.current_source_dir() / 'static/js/blame_incremental.js',
  meson.current_source_dir() / 'static/js/javascript-detection.js',
  meson.current_source_dir() / 'static/js/lib/common-lib.js',
  meson.current_source_dir() / 'static/js/lib/cookies.js',
  meson.current_source_dir() / 'static/js/lib/datetime.js',
]

test_dependencies += custom_target(
  input: javascript_files,
  output: 'gitweb.js',
  command: [
    shell,
    meson.current_source_dir() / 'generate-gitweb-js.sh',
    '@OUTPUT@',
  ] + javascript_files,
  install: true,
  install_dir: get_option('datadir') / 'gitweb/static',
)

foreach asset : [
  'static/git-favicon.png',
  'static/git-logo.png',
  'static/gitweb.css',
]
  if meson.version().version_compare('>=1.3.0')
    fs.copyfile(asset,
      install: true,
      install_dir: get_option('datadir') / 'gitweb' / fs.parent(asset),
    )
  else
    configure_file(
      input: asset,
      output: fs.stem(asset),
      copy: true,
      install: true,
      install_dir: get_option('datadir') / 'gitweb' / fs.parent(asset),
    )
  endif
endforeach
