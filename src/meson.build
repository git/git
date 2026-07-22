libgit_rs_sources = [
  'csum_file.rs',
  'hash.rs',
  'lib.rs',
  'loose.rs',
  'varint.rs',
]

# Unfortunately we must use a wrapper command to move the output file into the
# current build directory. This can fixed once `cargo build --artifact-dir`
# stabilizes. See https://github.com/rust-lang/cargo/issues/6790 for that
# effort.
cargo_command = [
  shell,
  meson.current_source_dir() / 'cargo-meson.sh',
  meson.project_source_root(),
  meson.current_build_dir(),
]
if get_option('buildtype') == 'release'
  cargo_command += '--release'
endif

libgit_rs = custom_target('git_rs',
  input: libgit_rs_sources + [
    meson.project_source_root() / 'Cargo.toml',
  ],
  output: 'libgitcore.a',
  command: cargo_command,
)
libgit_dependencies += declare_dependency(link_with: libgit_rs)

if get_option('tests')
  test('rust', cargo,
    args: [
      'test',
      '--manifest-path',
      meson.project_source_root() / 'Cargo.toml',
      '--target-dir',
      meson.current_build_dir() / 'target',
    ],
    timeout: 0,
    protocol: 'rust',
  )
endif
