# This is a phony Perl program that's only here to test xgettext
# message extraction

# so the above comment won't be folded into the next one by xgettext
1;

# TRANSLATORS: This is a test. You don't need to translate it.
print __("TEST: A Perl test string");

# TRANSLATORS: This is a test. You don't need to translate it.
printf __("TEST: A Perl test variable %s"), "moo";

# TRANSLATORS: If you see this, Git has a bug
print _"TEST: A Perl string xgettext will not get";
