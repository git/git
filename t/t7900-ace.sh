#!/bin/sh

test_description='git ace manages branch trees'

. ./test-lib.sh

test_expect_success 'setup repository' '
	test_commit base &&
	git branch --show-current >root-branch &&
	git ace init
'

test_expect_success 'create nested branch metadata' '
	git ace create topic &&
	git ace checkout topic &&
	test_commit topic-work &&
	git ace create topic/parser &&
	git ace checkout topic/parser &&
	test_commit parser-work &&
	git ace create topic/parser/validation
'

test_expect_success 'parent and chain follow explicit metadata' '
	echo topic/parser >expected-parent &&
	git ace parent topic/parser/validation >actual-parent &&
	test_cmp expected-parent actual-parent &&
	root_branch=$(cat root-branch) &&
	cat >expected-chain <<-EOF &&
	$root_branch
	topic
	topic/parser
	topic/parser/validation
	EOF
	git ace chain topic/parser/validation >actual-chain &&
	test_cmp expected-chain actual-chain
'

test_expect_success 'recursive children and tree span nested hierarchy' '
	root_branch=$(cat root-branch) &&
	cat >expected-children <<-EOF &&
	topic
	topic/parser
	topic/parser/validation
	EOF
	git ace children --recursive "$root_branch" >actual-children &&
	test_cmp expected-children actual-children &&
	cat >expected-tree <<-EOF &&
	$root_branch
	  topic
	    topic/parser
	      topic/parser/validation
	EOF
	git ace tree topic/parser/validation >actual-tree &&
	test_cmp expected-tree actual-tree
'

test_expect_success 'set-parent rejects cycles' '
	test_must_fail git ace set-parent topic topic/parser/validation 2>err &&
	test_grep "refusing to create cycle" err &&
	test_must_fail git ace set-parent topic topic 2>err2 &&
	test_grep "refusing to create cycle" err2
'

test_expect_success 'rebase-stack rebases descendants onto updated parent' '
	git ace checkout topic &&
	test_commit topic-second &&
	git ace checkout topic/parser &&
	git ace rebase-stack topic &&
	topic_ref=$(git ace resolve topic) &&
	parser_ref=$(git ace resolve topic/parser) &&
	test "$(git merge-base "$topic_ref" "$parser_ref")" = "$(git rev-parse "$topic_ref")"
'

test_expect_success 'agent integration exposes Ace branch context' '
	write_script .git/ace-agent-stub <<-\EOF &&
	printf "%s|%s|%s|%s\n" "$ACE_AGENT_NAME" "$ACE_BRANCH" "$ACE_REAL_BRANCH" "$ACE_PARENT_BRANCH"
	printf "%s\n" "$*" >agent-args
	EOF
	git config ace.agent.opencode.command "$PWD/.git/ace-agent-stub" &&
	parser_ref=$(git ace resolve topic/parser) &&
	echo "opencode|topic/parser|$parser_ref|topic" >expected-agent &&
	git ace agent run opencode topic/parser -- fulfill end user need >actual-agent &&
	test_cmp expected-agent actual-agent &&
	echo "fulfill end user need" >expected-args &&
	test_cmp expected-args agent-args
'

test_expect_success 'agent list includes Claude Code and OpenCode' '
	cat >expected-agents <<-EOF &&
	claude-code
	opencode
	EOF
	git ace agent list >actual-agents &&
	test_cmp expected-agents actual-agents
'

test_expect_success 'Mercurial bookmark export writes Ace branch pointers' '
	root_branch=$(cat root-branch) &&
	root_ref=$(git rev-parse "$root_branch") &&
	topic_ref=$(git rev-parse "$(git ace resolve topic)") &&
	parser_ref=$(git rev-parse "$(git ace resolve topic/parser)") &&
	validation_ref=$(git rev-parse "$(git ace resolve topic/parser/validation)") &&
	git ace hg export-bookmarks hg-bookmarks &&
	cat >expected-bookmarks <<-EOF &&
	$topic_ref topic
	$parser_ref topic/parser
	$validation_ref topic/parser/validation
	EOF
	test_cmp expected-bookmarks hg-bookmarks
'

test_expect_success 'Mercurial bookmark import creates and updates Ace branches' '
	git rev-parse HEAD >import-oid &&
	import_oid=$(cat import-oid) &&
	cat >import-bookmarks <<-EOF &&
	$import_oid imported/topic
	$import_oid imported/topic/parser
	EOF
	git ace hg import-bookmarks import-bookmarks &&
	echo imported/topic >expected-parent-import &&
	git ace parent imported/topic/parser >actual-parent-import &&
	test_cmp expected-parent-import actual-parent-import &&
	test "$(git rev-parse "$(git ace resolve imported/topic)")" = "$import_oid" &&
	test "$(git rev-parse "$(git ace resolve imported/topic/parser)")" = "$import_oid"
'

test_expect_success 'Mercurial named branch export writes root Ace branch tips' '
	imported_ref=$(git rev-parse "$(git ace resolve imported/topic)") &&
	git ace hg export-named-branches hg-named-branches &&
	cat >expected-named-branches <<-EOF &&
	$imported_ref imported/topic
	EOF
	test_cmp expected-named-branches hg-named-branches
'

test_expect_success 'Mercurial named branch import creates root Ace branches' '
	git rev-parse HEAD >named-oid &&
	named_oid=$(cat named-oid) &&
	cat >import-named-branches <<-EOF &&
	$named_oid release
	EOF
	git ace hg import-named-branches import-named-branches &&
	git ace parent release >actual-release-parent &&
	test_must_be_empty actual-release-parent &&
	test "$(git rev-parse "$(git ace resolve release)")" = "$named_oid"
'

test_expect_success 'rename updates descendant metadata and backing refs' '
	parser_oid=$(git rev-parse "$(git ace resolve topic/parser)") &&
	git ace rename topic epic &&
	echo epic/parser >expected-renamed-parent &&
	git ace parent epic/parser/validation >actual-renamed-parent &&
	test_cmp expected-renamed-parent actual-renamed-parent &&
	cat >expected-renamed-tree <<-EOF &&
	$(cat root-branch)
	  epic
	    epic/parser
	      epic/parser/validation
	EOF
	git ace tree epic/parser/validation >actual-renamed-tree &&
	test_cmp expected-renamed-tree actual-renamed-tree &&
	test_must_fail git ace resolve topic &&
	git ace resolve epic/parser >actual-renamed-parser-ref &&
	test "$(git rev-parse "$(cat actual-renamed-parser-ref)")" = "$parser_oid" &&
	git ace checkout epic/parser
'

test_expect_success 'delete refuses subtree removal without explicit flag' '
	test_must_fail git ace delete epic 2>delete-subtree.err &&
	test_grep "has descendants" delete-subtree.err
'

test_expect_success 'delete with descendants removes subtree' '
	git ace resolve epic >epic-ref &&
	git ace resolve epic/parser >epic-parser-ref &&
	git ace resolve epic/parser/validation >epic-validation-ref &&
	git ace delete --with-descendants --force epic
'

test_expect_success 'delete with descendants removes subtree metadata and refs' '
	epic_ref=$(cat epic-ref) &&
	parser_ref=$(cat epic-parser-ref) &&
	validation_ref=$(cat epic-validation-ref) &&
	test ! -e .git/ace/branches/epic &&
	! git show-ref --verify --quiet "refs/heads/$epic_ref" &&
	! git show-ref --verify --quiet "refs/heads/$parser_ref" &&
	! git show-ref --verify --quiet "refs/heads/$validation_ref"
'

test_expect_success 'delete with descendants checks out fallback branch if current branch is deleted' '
	git checkout "$(cat root-branch)" &&
	git ace create temp-tree &&
	git ace checkout temp-tree &&
	git ace create temp-tree/child &&
	git ace checkout temp-tree/child &&
	git ace delete --with-descendants --force temp-tree &&
	test_must_fail git ace resolve temp-tree/child &&
	test "$(git branch --show-current)" = "$(cat root-branch)"
'

test_done
