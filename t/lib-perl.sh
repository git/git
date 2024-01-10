# Copyright (c) 2022 Ævar Arnfjörð Bjarmason

test_lazy_prereq PERL_TEST_MORE '
	perl -MTest::More -e 0
'

skip_all_if_no_Test_More () {
	if ! test_have_prereq PERL
	then
		skip_all='skipping perl interface tests, perl not available'
		test_done
	fi

	if ! test_have_prereq PERL_TEST_MORE
	then
		skip_all="Perl Test::More unavailable, skipping test"
		test_done
	fi
}
