file(REMOVE_RECURSE
  "t/unit-tests/bin/unit-tests"
  "t/unit-tests/bin/unit-tests.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/unit-tests.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
