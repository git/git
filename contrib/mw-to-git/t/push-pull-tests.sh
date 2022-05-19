test_push_pull () {

	test_expect_success 'Git pull works after adding a new wiki page' '
		wiki_reset &&

		but clone mediawiki::'"$WIKI_URL"' mw_dir_1 &&
		wiki_editpage Foo "page created after the but clone" false &&

		(
			cd mw_dir_1 &&
			but pull
		) &&

		wiki_getallpage ref_page_1 &&
		test_diff_directories mw_dir_1 ref_page_1
	'

	test_expect_success 'Git pull works after editing a wiki page' '
		wiki_reset &&

		wiki_editpage Foo "page created before the but clone" false &&
		but clone mediawiki::'"$WIKI_URL"' mw_dir_2 &&
		wiki_editpage Foo "new line added on the wiki" true &&

		(
			cd mw_dir_2 &&
			but pull
		) &&

		wiki_getallpage ref_page_2 &&
		test_diff_directories mw_dir_2 ref_page_2
	'

	test_expect_success 'but pull works on conflict handled by auto-merge' '
		wiki_reset &&

		wiki_editpage Foo "1 init
3
5
	" false &&
		but clone mediawiki::'"$WIKI_URL"' mw_dir_3 &&

		wiki_editpage Foo "1 init
2 content added on wiki after clone
3
5
	" false &&

		(
			cd mw_dir_3 &&
		echo "1 init
3
4 content added on but after clone
5
" >Foo.mw &&
			but cummit -am "conflicting change on foo" &&
			but pull &&
			but push
		)
	'

	test_expect_success 'Git push works after adding a file .mw' '
		wiki_reset &&
		but clone mediawiki::'"$WIKI_URL"' mw_dir_4 &&
		wiki_getallpage ref_page_4 &&
		(
			cd mw_dir_4 &&
			test_path_is_missing Foo.mw &&
			touch Foo.mw &&
			echo "hello world" >>Foo.mw &&
			but add Foo.mw &&
			but cummit -m "Foo" &&
			but push
		) &&
		wiki_getallpage ref_page_4 &&
		test_diff_directories mw_dir_4 ref_page_4
	'

	test_expect_success 'Git push works after editing a file .mw' '
		wiki_reset &&
		wiki_editpage "Foo" "page created before the but clone" false &&
		but clone mediawiki::'"$WIKI_URL"' mw_dir_5 &&

		(
			cd mw_dir_5 &&
			echo "new line added in the file Foo.mw" >>Foo.mw &&
			but cummit -am "edit file Foo.mw" &&
			but push
		) &&

		wiki_getallpage ref_page_5 &&
		test_diff_directories mw_dir_5 ref_page_5
	'

	test_expect_failure 'Git push works after deleting a file' '
		wiki_reset &&
		wiki_editpage Foo "wiki page added before but clone" false &&
		but clone mediawiki::'"$WIKI_URL"' mw_dir_6 &&

		(
			cd mw_dir_6 &&
			but rm Foo.mw &&
			but cummit -am "page Foo.mw deleted" &&
			but push
		) &&

		test_must_fail wiki_page_exist Foo
	'

	test_expect_success 'Merge conflict expected and solving it' '
		wiki_reset &&

		but clone mediawiki::'"$WIKI_URL"' mw_dir_7 &&
		wiki_editpage Foo "1 conflict
3 wiki
4" false &&

		(
			cd mw_dir_7 &&
		echo "1 conflict
2 but
4" >Foo.mw &&
			but add Foo.mw &&
			but cummit -m "conflict created" &&
			test_must_fail but pull &&
			"$PERL_PATH" -pi -e "s/[<=>].*//g" Foo.mw &&
			but cummit -am "merge conflict solved" &&
			but push
		)
	'

	test_expect_failure 'but pull works after deleting a wiki page' '
		wiki_reset &&
		wiki_editpage Foo "wiki page added before the but clone" false &&
		but clone mediawiki::'"$WIKI_URL"' mw_dir_8 &&

		wiki_delete_page Foo &&
		(
			cd mw_dir_8 &&
			but pull &&
			test_path_is_missing Foo.mw
		)
	'
}
