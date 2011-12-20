# This is a phony Shell program that's only here to test xgettext
# message extraction

# so the above comment won't be folded into the next one by xgettext
echo

# TRANSLATORS: This is a test. You don't need to translate it.
gettext "TEST: A Shell test string"

# TRANSLATORS: This is a test. You don't need to translate it.
eval_gettext "TEST: A Shell test \$variable"

# TRANSLATORS: If you see this, Git has a bug
_("TEST: A Shell string xgettext won't get")
