void test_strbuf(void)
{
	struct strbuf sb3 = STRBUF_INIT;
	struct strbuf sb4 = STRBUF_INIT;
	struct strbuf sb7 = STRBUF_INIT;
	struct strbuf *sp1;
	struct strbuf *sp3;
	struct strbuf *sp6 = xmalloc(sizeof(struct strbuf));
	strbuf_init(sp1, 0);
	strbuf_init(sp3, 0);
	strbuf_init(sp6, 0);

	use_before(&sb3);
	use_as_str("%s", sb7.buf);
	use_as_str("%s", sp1->buf);
	use_as_str("%s", sp6->buf);
	pass_pp(&sp3);

	strbuf_release(&sb3);
	strbuf_release(&sb4);
	strbuf_release(&sb7);
	strbuf_release(sp1);
	strbuf_release(sp3);
	strbuf_release(sp6);

	use_after(&sb4);

	if (when_strict())
		return;
}

void test_other(void)
{
}

void test_worktrees(void)
{
	struct worktree **w4;

	w4 = get_worktrees();

	use_it(w4);

	free_worktrees(w4);
}
