libgit_rs_sources = [
  'src/csum_file.rs',
  'src/hash.rs',
  'src/lib.rs',
  'src/loose.rs',
  'src/varint.rs',
]

# Unfortunately we must use a wrapper command to move the output file into the
# current build directory. This can fixed once `cargo build --artifact-dir`
# stabilizes. See https://github.com/rust-lang/cargo/issues/6790 for that
# effort.
cargo_command = [
  shell,
  meson.current_source_dir() / 'cargo-meson.sh',
  meson.current_source_dir(),
  meson.current_build_dir(),
]
if get_option('buildtype') == 'release'
  cargo_command += '--release'
endif

libgit_rs = custom_target('git_rs',
  input: libgit_rs_sources + [
    meson.current_source_dir() / 'Cargo.toml',
  ],
  output: 'libgitcore.a',
  command: cargo_command,
)
libgit_dependencies += declare_dependency(link_with: libgit_rs)

cargo = find_program('cargo', dirs: program_path, native: true, required: get_option('rust'))
if get_option('tests')
  test('rust', cargo,
    args: [
      'test',
      '--manifest-path',
      meson.current_source_dir() / 'Cargo.toml',
      '--target-dir',
      meson.current_build_dir() / 'target',
    ],
    timeout: 0,
    protocol: 'rust',
  )
endif
