fuzz_programs = [
  'fuzz-commit-graph.c',
  'fuzz-config.c',
  'fuzz-credential-from-url-gently.c',
  'fuzz-date.c',
  'fuzz-pack-headers.c',
  'fuzz-pack-idx.c',
  'fuzz-parse-attr-line.c',
  'fuzz-url-decode-mem.c',
]

foreach fuzz_program : fuzz_programs
  executable(fs.stem(fuzz_program),
    sources: [
      'dummy-cmd-main.c',
      fuzz_program,
    ],
    dependencies: [libgit_commonmain],
  )
endforeach
