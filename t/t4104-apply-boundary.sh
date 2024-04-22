#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git apply boundary tests'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

L="c d e f g h i j k l m n o p q r s t u v w x"

test_expect_success setup '
	test_write_lines b $L y >victim &&
	cat victim >original &&
	git update-index --add victim &&

	# add to the head
	test_write_lines a b $L y >victim &&
	cat victim >add-a-expect &&
	git diff victim >add-a-patch.with &&
	git diff --unified=0 >add-a-patch.without &&

	# insert at line two
	test_write_lines b a $L y >victim &&
	cat victim >insert-a-expect &&
	git diff victim >insert-a-patch.with &&
	git diff --unified=0 >insert-a-patch.without &&

	# modify at the head
	test_write_lines a $L y >victim &&
	cat victim >mod-a-expect &&
	git diff victim >mod-a-patch.with &&
	git diff --unified=0 >mod-a-patch.without &&

	# remove from the head
	test_write_lines $L y >victim &&
	cat victim >del-a-expect &&
	git diff victim >del-a-patch.with &&
	git diff --unified=0 >del-a-patch.without &&

	# add to the tail
	test_write_lines b $L y z >victim &&
	cat victim >add-z-expect &&
	git diff victim >add-z-patch.with &&
	git diff --unified=0 >add-z-patch.without &&

	# modify at the tail
	test_write_lines b $L z >victim &&
	cat victim >mod-z-expect &&
	git diff victim >mod-z-patch.with &&
	git diff --unified=0 >mod-z-patch.without &&

	# remove from the tail
	test_write_lines b $L >victim &&
	cat victim >del-z-expect &&
	git diff victim >del-z-patch.with &&
	git diff --unified=0 >del-z-patch.without

	# done
'

for with in with without
do
	case "$with" in
	with) u= ;;
	without) u=--unidiff-zero ;;
	esac
	for kind in add-a add-z insert-a mod-a mod-z del-a del-z
	do
		test_expect_success "apply $kind-patch $with context" '
			cat original >victim &&
			git update-index victim &&
			git apply --index $u "$kind-patch.$with" &&
			test_cmp "$kind-expect" victim
		'
	done
done

for kind in add-a add-z insert-a mod-a mod-z del-a del-z
do
	rm -f $kind-ng.without
	sed	-e "s/^diff --git /diff /" \
		-e '/^index /d' \
		<$kind-patch.without >$kind-ng.without
	test_expect_success "apply non-git $kind-patch without context" '
		cat original >victim &&
		git update-index victim &&
		git apply --unidiff-zero --index "$kind-ng.without" &&
		test_cmp "$kind-expect" victim
	'
done

test_expect_success 'two lines' '
	>file &&
	git add file &&
	echo aaa >file &&
	git diff >patch &&
	git add file &&
	echo bbb >file &&
	git add file &&
	test_must_fail git apply --check patch
'

test_expect_success 'apply patch with 3 context lines matching at end' '
	test_write_lines a b c d >file &&
	git add file &&
	echo e >>file &&
	git diff >patch &&
	>file &&
	test_must_fail git apply patch
'

test_done
