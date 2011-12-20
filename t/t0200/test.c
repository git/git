/* This is a phony C program that's only here to test xgettext message extraction */

const char help[] =
	/* TRANSLATORS: This is a test. You don't need to translate it. */
	N_("See 'git help COMMAND' for more information on a specific command.");

int main(void)
{
	/* TRANSLATORS: This is a test. You don't need to translate it. */
	puts(_("TEST: A C test string"));

	/* TRANSLATORS: This is a test. You don't need to translate it. */
	printf(_("TEST: A C test string %s"), "variable");

	/* TRANSLATORS: This is a test. You don't need to translate it. */
	printf(_("TEST: Hello World!"));

	/* TRANSLATORS: This is a test. You don't need to translate it. */
	printf(_("TEST: Old English Runes"));

	/* TRANSLATORS: This is a test. You don't need to translate it. */
	printf(_("TEST: ‘single’ and “double” quotes"));
}
