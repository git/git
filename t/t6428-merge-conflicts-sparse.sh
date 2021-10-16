#!/bin/sh

test_description="merge cases"

# The setup for all of them, pictorially, is:
#
#      A
#      o
#     / \
#  O o   ?
#     \ /
#      o
#      B
#
# To help make it easier to follow the flow of tests, they have been
# divided into sections and each test will start with a quick explanation
# of what commits O, A, and B contain.
#
# Notation:
#    z/{b,c}   means  files z/b and z/c both exist
#    x/d_1     means  file x/d exists with content d1.  (Purpose of the
#                     underscore notation is to differentiate different
#                     files that might be renamed into each other's paths.)

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-merge.sh


# Testcase basic, conflicting changes in 'numerals'

test_setup_numerals () {
	test_create_repo numerals_$1 &&
	(
		cd numerals_$1 &&

		>README &&
		test_write_lines I II III >numerals &&
		git add README numerals &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_write_lines I II III IIII >numerals &&
		git add numerals &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		test_write_lines I II III IV >numerals &&
		git add numerals &&
		test_tick &&
		git commit -m "B" &&

		cat <<-EOF >expected-index &&
		H README
		M numerals
		M numerals
		M numerals
		EOF

		cat <<-EOF >expected-merge
		I
		II
		III
		<<<<<<< HEAD
		IIII
		=======
		IV
		>>>>>>> B^0
		EOF

	)
}

test_expect_success 'conflicting entries written to worktree even if sparse' '
	test_setup_numerals plain &&
	(
		cd numerals_plain &&

		git checkout A^0 &&

		test_path_is_file README &&
		test_path_is_file numerals &&

		git sparse-checkout init &&
		git sparse-checkout set README &&

		test_path_is_file README &&
		test_path_is_missing numerals &&

		test_must_fail git merge -s recursive B^0 &&

		git ls-files -t >index_files &&
		test_cmp expected-index index_files &&

		test_path_is_file README &&
		test_path_is_file numerals &&

		test_cmp expected-merge numerals &&

		# 4 other files:
		#   * expected-merge
		#   * expected-index
		#   * index_files
		#   * others
		git ls-files -o >others &&
		test_line_count = 4 others
	)
'

test_expect_merge_algorithm failure success 'present-despite-SKIP_WORKTREE handled reasonably' '
	test_setup_numerals in_the_way &&
	(
		cd numerals_in_the_way &&

		git checkout A^0 &&

		test_path_is_file README &&
		test_path_is_file numerals &&

		git sparse-checkout init &&
		git sparse-checkout set README &&

		test_path_is_file README &&
		test_path_is_missing numerals &&

		echo foobar >numerals &&

		test_must_fail git merge -s recursive B^0 &&

		git ls-files -t >index_files &&
		test_cmp expected-index index_files &&

		test_path_is_file README &&
		test_path_is_file numerals &&

		test_cmp expected-merge numerals &&

		# There should still be a file with "foobar" in it
		grep foobar * &&

		# 5 other files:
		#   * expected-merge
		#   * expected-index
		#   * index_files
		#   * others
		#   * whatever name was given to the numerals file that had
		#     "foobar" in it
		git ls-files -o >others &&
		test_line_count = 5 others
	)
'

test_done
