#!/bin/sh
#
# Perform style check
#

baseCommit=$1

# Remove optional braces of control statements (if, else, for, and while)
# according to the LLVM coding style. This avoids braces on simple
# single-statement bodies of statements but keeps braces if one side of
# if/else if/.../else cascade has multi-statement body.
#
# As this rule comes with a warning [1], we want to experiment with it
# before adding it in-tree. since the CI job for the style check is allowed
# to fail, appending the rule here allows us to validate its efficacy.
# While also ensuring that end-users are not affected directly.
#
# [1]: https://clang.llvm.org/docs/ClangFormatStyleOptions.html#removebracesllvm
{
	cat .clang-format
	echo "RemoveBracesLLVM: true"
} >/tmp/clang-format-rules

git clang-format --style=file:/tmp/clang-format-rules \
	--diff --extensions c,h "$baseCommit"
