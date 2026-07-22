#!/bin/sh

test_description='git blame with specific diff algorithm'

. ./test-lib.sh

test_expect_success setup '
	cat >file.c <<-\EOF &&
	int f(int x, int y)
	{
	  if (x == 0)
	  {
	    return y;
	  }
	  return x;
	}

	int g(size_t u)
	{
	  while (u < 30)
	  {
	    u++;
	  }
	  return u;
	}
	EOF
	test_write_lines x x x x >file.txt &&
	git add file.c file.txt &&
	GIT_AUTHOR_NAME=Commit_1 git commit -m Commit_1 &&

	cat >file.c <<-\EOF &&
	int g(size_t u)
	{
	  while (u < 30)
	  {
	    u++;
	  }
	  return u;
	}

	int h(int x, int y, int z)
	{
	  if (z == 0)
	  {
	    return x;
	  }
	  return y;
	}
	EOF
	test_write_lines x x x A B C D x E F G >file.txt &&
	git add file.c file.txt &&
	GIT_AUTHOR_NAME=Commit_2 git commit -m Commit_2
'

test_expect_success 'blame uses Myers diff algorithm by default' '
	cat >expected <<-\EOF &&
	Commit_2 int g(size_t u)
	Commit_1 {
	Commit_2   while (u < 30)
	Commit_1   {
	Commit_2     u++;
	Commit_1   }
	Commit_2   return u;
	Commit_1 }
	Commit_1
	Commit_2 int h(int x, int y, int z)
	Commit_1 {
	Commit_2   if (z == 0)
	Commit_1   {
	Commit_2     return x;
	Commit_1   }
	Commit_2   return y;
	Commit_1 }
	EOF

	git blame file.c >output &&
	sed -e "s/^[^ ]* (\([^ ]*\) [^)]*)/\1/g" output >without_varying_parts &&
	sed -e "s/ *$//g" without_varying_parts >actual &&
	test_cmp expected actual
'

test_expect_success 'blame honors --diff-algorithm option' '
	cat >expected <<-\EOF &&
	Commit_1 int g(size_t u)
	Commit_1 {
	Commit_1   while (u < 30)
	Commit_1   {
	Commit_1     u++;
	Commit_1   }
	Commit_1   return u;
	Commit_1 }
	Commit_2
	Commit_2 int h(int x, int y, int z)
	Commit_2 {
	Commit_2   if (z == 0)
	Commit_2   {
	Commit_2     return x;
	Commit_2   }
	Commit_2   return y;
	Commit_2 }
	EOF

	git blame file.c --diff-algorithm histogram >output &&
	sed -e "s/^[^ ]* (\([^ ]*\) [^)]*)/\1/g" output >without_varying_parts &&
	sed -e "s/ *$//g" without_varying_parts >actual &&
	test_cmp expected actual
'

test_expect_success 'blame honors diff.algorithm config variable' '
	cat >expected <<-\EOF &&
	Commit_1 int g(size_t u)
	Commit_1 {
	Commit_1   while (u < 30)
	Commit_1   {
	Commit_1     u++;
	Commit_1   }
	Commit_1   return u;
	Commit_1 }
	Commit_2
	Commit_2 int h(int x, int y, int z)
	Commit_2 {
	Commit_2   if (z == 0)
	Commit_2   {
	Commit_2     return x;
	Commit_2   }
	Commit_2   return y;
	Commit_2 }
	EOF

	git -c diff.algorithm=histogram blame file.c >output &&
	sed -e "s/^[^ ]* (\([^ ]*\) [^)]*)/\1/g" \
	    -e "s/ *$//g" output >actual &&
	test_cmp expected actual
'

test_expect_success 'blame gives priority to --diff-algorithm over diff.algorithm' '
	cat >expected <<-\EOF &&
	Commit_1 int g(size_t u)
	Commit_1 {
	Commit_1   while (u < 30)
	Commit_1   {
	Commit_1     u++;
	Commit_1   }
	Commit_1   return u;
	Commit_1 }
	Commit_2
	Commit_2 int h(int x, int y, int z)
	Commit_2 {
	Commit_2   if (z == 0)
	Commit_2   {
	Commit_2     return x;
	Commit_2   }
	Commit_2   return y;
	Commit_2 }
	EOF

	git -c diff.algorithm=myers blame file.c --diff-algorithm histogram >output &&
	sed -e "s/^[^ ]* (\([^ ]*\) [^)]*)/\1/g" \
	    -e "s/ *$//g" output >actual &&
	test_cmp expected actual
'

test_expect_success 'blame honors --minimal option' '
	cat >expected <<-\EOF &&
	Commit_1 x
	Commit_1 x
	Commit_1 x
	Commit_2 A
	Commit_2 B
	Commit_2 C
	Commit_2 D
	Commit_1 x
	Commit_2 E
	Commit_2 F
	Commit_2 G
	EOF

	git blame file.txt --minimal >output &&
	sed -e "s/^[^ ]* (\([^ ]*\) [^)]*)/\1/g" output >actual &&
	test_cmp expected actual
'

test_expect_success 'blame respects the order of diff options' '
	cat >expected <<-\EOF &&
	Commit_1 x
	Commit_1 x
	Commit_1 x
	Commit_2 A
	Commit_2 B
	Commit_2 C
	Commit_2 D
	Commit_2 x
	Commit_2 E
	Commit_2 F
	Commit_2 G
	EOF

	git blame file.txt --minimal --diff-algorithm myers >output &&
	sed -e "s/^[^ ]* (\([^ ]*\) [^)]*)/\1/g" output >actual &&
	test_cmp expected actual
'

test_done
