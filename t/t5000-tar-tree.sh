#!/bin/sh
#
# Copyright (C) 2005 Rene Scharfe
#

test_description='git archive and git get-tar-commit-id test

This test covers the topics of file contents, commit date handling and
commit id embedding:

  The contents of the repository is compared to the extracted tar
  archive.  The repository contains simple text files, symlinks and a
  binary file (/bin/sh).  Only paths shorter than 99 characters are
  used.

  git archive applies the commit date to every file in the archive it
  creates.  The test sets the commit date to a specific value and checks
  if the tar archive contains that value.

  When giving git archive a commit id (in contrast to a tree id) it
  embeds this commit id into the tar archive as a comment.  The test
  checks the ability of git get-tar-commit-id to figure it out from the
  tar file.

'

. ./test-lib.sh

SUBSTFORMAT=%H%n

test_lazy_prereq TAR_NEEDS_PAX_FALLBACK '
	(
		mkdir pax &&
		cd pax &&
		"$TAR" xf "$TEST_DIRECTORY"/t5000/pax.tar &&
		test -f PaxHeaders.1791/file
	)
'

test_lazy_prereq GZIP 'gzip --version'

get_pax_header() {
	file=$1
	header=$2=

	while read len rest
	do
		if test "$len" = $(echo "$len $rest" | wc -c)
		then
			case "$rest" in
			$header*)
				echo "${rest#$header}"
				;;
			esac
		fi
	done <"$file"
}

check_tar() {
	tarfile=$1.tar
	listfile=$1.lst
	dir=$1
	dir_with_prefix=$dir/$2

	test_expect_success ' extract tar archive' '
		(mkdir $dir && cd $dir && "$TAR" xf -) <$tarfile
	'

	test_expect_success TAR_NEEDS_PAX_FALLBACK ' interpret pax headers' '
		(
			cd $dir &&
			for header in *.paxheader
			do
				data=${header%.paxheader}.data &&
				if test -h $data || test -e $data
				then
					path=$(get_pax_header $header path) &&
					if test -n "$path"
					then
						mv "$data" "$path"
					fi
				fi
			done
		)
	'

	test_expect_success ' validate filenames' '
		(cd ${dir_with_prefix}a && find .) | sort >$listfile &&
		test_cmp a.lst $listfile
	'

	test_expect_success ' validate file contents' '
		diff -r a ${dir_with_prefix}a
	'
}

test_expect_success \
    'populate workdir' \
    'mkdir a &&
     echo simple textfile >a/a &&
     ten=0123456789 && hundred=$ten$ten$ten$ten$ten$ten$ten$ten$ten$ten &&
     echo long filename >a/four$hundred &&
     mkdir a/bin &&
     test-genrandom "frotz" 500000 >a/bin/sh &&
     printf "A\$Format:%s\$O" "$SUBSTFORMAT" >a/substfile1 &&
     printf "A not substituted O" >a/substfile2 &&
     if test_have_prereq SYMLINKS; then
	ln -s a a/l1
     else
	printf %s a > a/l1
     fi &&
     (p=long_path_to_a_file && cd a &&
      for depth in 1 2 3 4 5; do mkdir $p && cd $p; done &&
      echo text >file_with_long_path) &&
     (cd a && find .) | sort >a.lst'

test_expect_success \
    'add ignored file' \
    'echo ignore me >a/ignored &&
     echo ignored export-ignore >.git/info/attributes'

test_expect_success 'add files to repository' '
	git add a &&
	GIT_COMMITTER_DATE="2005-05-27 22:00" git commit -m initial
'

test_expect_success 'setup export-subst' '
	echo "substfile?" export-subst >>.git/info/attributes &&
	git log --max-count=1 "--pretty=format:A${SUBSTFORMAT}O" HEAD \
		>a/substfile1
'

test_expect_success \
    'create bare clone' \
    'git clone --bare . bare.git &&
     cp .git/info/attributes bare.git/info/attributes'

test_expect_success \
    'remove ignored file' \
    'rm a/ignored'

test_expect_success \
    'git archive' \
    'git archive HEAD >b.tar'

check_tar b

test_expect_success 'git archive --prefix=prefix/' '
	git archive --prefix=prefix/ HEAD >with_prefix.tar
'

check_tar with_prefix prefix/

test_expect_success 'git-archive --prefix=olde-' '
	git archive --prefix=olde- HEAD >with_olde-prefix.tar
'

check_tar with_olde-prefix olde-

test_expect_success 'git archive on large files' '
    test_config core.bigfilethreshold 1 &&
    git archive HEAD >b3.tar &&
    test_cmp_bin b.tar b3.tar
'

test_expect_success \
    'git archive in a bare repo' \
    '(cd bare.git && git archive HEAD) >b3.tar'

test_expect_success \
    'git archive vs. the same in a bare repo' \
    'test_cmp_bin b.tar b3.tar'

test_expect_success 'git archive with --output' \
    'git archive --output=b4.tar HEAD &&
    test_cmp_bin b.tar b4.tar'

test_expect_success 'git archive --remote' \
    'git archive --remote=. HEAD >b5.tar &&
    test_cmp_bin b.tar b5.tar'

test_expect_success \
    'validate file modification time' \
    'mkdir extract &&
     "$TAR" xf b.tar -C extract a/a &&
     test-chmtime -v +0 extract/a/a |cut -f 1 >b.mtime &&
     echo "1117231200" >expected.mtime &&
     test_cmp expected.mtime b.mtime'

test_expect_success \
    'git get-tar-commit-id' \
    'git get-tar-commit-id <b.tar >b.commitid &&
     test_cmp .git/$(git symbolic-ref HEAD) b.commitid'

test_expect_success 'git archive with --output, override inferred format' '
	git archive --format=tar --output=d4.zip HEAD &&
	test_cmp_bin b.tar d4.zip
'

test_expect_success \
    'git archive --list outside of a git repo' \
    'GIT_DIR=some/non-existing/directory git archive --list'

test_expect_success 'clients cannot access unreachable commits' '
	test_commit unreachable &&
	sha1=$(git rev-parse HEAD) &&
	git reset --hard HEAD^ &&
	git archive $sha1 >remote.tar &&
	test_must_fail git archive --remote=. $sha1 >remote.tar
'

test_expect_success 'upload-archive can allow unreachable commits' '
	test_commit unreachable1 &&
	sha1=$(git rev-parse HEAD) &&
	git reset --hard HEAD^ &&
	git archive $sha1 >remote.tar &&
	test_config uploadarchive.allowUnreachable true &&
	git archive --remote=. $sha1 >remote.tar
'

test_expect_success 'setup tar filters' '
	git config tar.tar.foo.command "tr ab ba" &&
	git config tar.bar.command "tr ab ba" &&
	git config tar.bar.remote true &&
	git config tar.invalid baz
'

test_expect_success 'archive --list mentions user filter' '
	git archive --list >output &&
	grep "^tar\.foo\$" output &&
	grep "^bar\$" output
'

test_expect_success 'archive --list shows only enabled remote filters' '
	git archive --list --remote=. >output &&
	! grep "^tar\.foo\$" output &&
	grep "^bar\$" output
'

test_expect_success 'invoke tar filter by format' '
	git archive --format=tar.foo HEAD >config.tar.foo &&
	tr ab ba <config.tar.foo >config.tar &&
	test_cmp_bin b.tar config.tar &&
	git archive --format=bar HEAD >config.bar &&
	tr ab ba <config.bar >config.tar &&
	test_cmp_bin b.tar config.tar
'

test_expect_success 'invoke tar filter by extension' '
	git archive -o config-implicit.tar.foo HEAD &&
	test_cmp_bin config.tar.foo config-implicit.tar.foo &&
	git archive -o config-implicit.bar HEAD &&
	test_cmp_bin config.tar.foo config-implicit.bar
'

test_expect_success 'default output format remains tar' '
	git archive -o config-implicit.baz HEAD &&
	test_cmp_bin b.tar config-implicit.baz
'

test_expect_success 'extension matching requires dot' '
	git archive -o config-implicittar.foo HEAD &&
	test_cmp_bin b.tar config-implicittar.foo
'

test_expect_success 'only enabled filters are available remotely' '
	test_must_fail git archive --remote=. --format=tar.foo HEAD \
		>remote.tar.foo &&
	git archive --remote=. --format=bar >remote.bar HEAD &&
	test_cmp_bin remote.bar config.bar
'

test_expect_success GZIP 'git archive --format=tgz' '
	git archive --format=tgz HEAD >j.tgz
'

test_expect_success GZIP 'git archive --format=tar.gz' '
	git archive --format=tar.gz HEAD >j1.tar.gz &&
	test_cmp_bin j.tgz j1.tar.gz
'

test_expect_success GZIP 'infer tgz from .tgz filename' '
	git archive --output=j2.tgz HEAD &&
	test_cmp_bin j.tgz j2.tgz
'

test_expect_success GZIP 'infer tgz from .tar.gz filename' '
	git archive --output=j3.tar.gz HEAD &&
	test_cmp_bin j.tgz j3.tar.gz
'

test_expect_success GZIP 'extract tgz file' '
	gzip -d -c <j.tgz >j.tar &&
	test_cmp_bin b.tar j.tar
'

test_expect_success GZIP 'remote tar.gz is allowed by default' '
	git archive --remote=. --format=tar.gz HEAD >remote.tar.gz &&
	test_cmp_bin j.tgz remote.tar.gz
'

test_expect_success GZIP 'remote tar.gz can be disabled' '
	git config tar.tar.gz.remote false &&
	test_must_fail git archive --remote=. --format=tar.gz HEAD \
		>remote.tar.gz
'

test_expect_success 'archive and :(glob)' '
	git archive -v HEAD -- ":(glob)**/sh" >/dev/null 2>actual &&
	cat >expect <<EOF &&
a/
a/bin/
a/bin/sh
EOF
	test_cmp expect actual
'

test_expect_success 'catch non-matching pathspec' '
	test_must_fail git archive -v HEAD -- "*.abc" >/dev/null
'

# Pull the size and date of each entry in a tarfile using the system tar.
#
# We'll pull out only the year from the date; that avoids any question of
# timezones impacting the result (as long as we keep our test times away from a
# year boundary; our reference times are all in August).
#
# The output of tar_info is expected to be "<size> <year>", both in decimal. It
# ignores the return value of tar. We have to do this, because some of our test
# input is only partial (the real data is 64GB in some cases).
tar_info () {
	"$TAR" tvf "$1" |
	awk '{
		split($4, date, "-")
		print $3 " " date[1]
	}'
}

# See if our system tar can handle a tar file with huge sizes and dates far in
# the future, and that we can actually parse its output.
#
# The reference file was generated by GNU tar, and the magic time and size are
# both octal 01000000000001, which overflows normal ustar fields.
test_lazy_prereq TAR_HUGE '
	echo "68719476737 4147" >expect &&
	tar_info "$TEST_DIRECTORY"/t5000/huge-and-future.tar >actual &&
	test_cmp expect actual
'

test_expect_success LONG_IS_64BIT 'set up repository with huge blob' '
	obj_d=19 &&
	obj_f=f9c8273ec45a8938e6999cb59b3ff66739902a &&
	obj=${obj_d}${obj_f} &&
	mkdir -p .git/objects/$obj_d &&
	cp "$TEST_DIRECTORY"/t5000/$obj .git/objects/$obj_d/$obj_f &&
	rm -f .git/index &&
	git update-index --add --cacheinfo 100644,$obj,huge &&
	git commit -m huge
'

# We expect git to die with SIGPIPE here (otherwise we
# would generate the whole 64GB).
test_expect_success LONG_IS_64BIT 'generate tar with huge size' '
	{
		git archive HEAD
		echo $? >exit-code
	} | test_copy_bytes 4096 >huge.tar &&
	echo 141 >expect &&
	test_cmp expect exit-code
'

test_expect_success TAR_HUGE,LONG_IS_64BIT 'system tar can read our huge size' '
	echo 68719476737 >expect &&
	tar_info huge.tar | cut -d" " -f1 >actual &&
	test_cmp expect actual
'

test_expect_success LONG_IS_64BIT 'set up repository with far-future commit' '
	rm -f .git/index &&
	echo content >file &&
	git add file &&
	GIT_COMMITTER_DATE="@68719476737 +0000" \
		git commit -m "tempori parendum"
'

test_expect_success LONG_IS_64BIT 'generate tar with future mtime' '
	git archive HEAD >future.tar
'

test_expect_success TAR_HUGE,LONG_IS_64BIT 'system tar can read our future mtime' '
	echo 4147 >expect &&
	tar_info future.tar | cut -d" " -f2 >actual &&
	test_cmp expect actual
'

test_done
