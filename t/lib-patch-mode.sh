: included from t2016 and others

. ./test-lib.sh

set_state () {
	echo "$3" > "$1" &&
	git add "$1" &&
	echo "$2" > "$1"
}

save_state () {
	noslash="$(echo "$1" | tr / _)" &&
	cat "$1" > _worktree_"$noslash" &&
	git show :"$1" > _index_"$noslash"
}

set_and_save_state () {
	set_state "$@" &&
	save_state "$1"
}

verify_state () {
	test "$(cat "$1")" = "$2" &&
	test "$(git show :"$1")" = "$3"
}

verify_saved_state () {
	noslash="$(echo "$1" | tr / _)" &&
	verify_state "$1" "$(cat _worktree_"$noslash")" "$(cat _index_"$noslash")"
}

save_head () {
	git rev-parse HEAD > _head
}

verify_saved_head () {
	test "$(cat _head)" = "$(git rev-parse HEAD)"
}
