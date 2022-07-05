void test_strbuf(void)
{
	struct strbuf sb1 = STRBUF_INIT;
	struct strbuf sb2 = STRBUF_INIT;
	struct strbuf sb3 = STRBUF_INIT;
	struct strbuf sb4 = STRBUF_INIT;
	struct strbuf sb5;
	struct strbuf sb6 = { 0 };
	struct strbuf sb7 = STRBUF_INIT;
	struct strbuf sb8 = STRBUF_INIT;
	struct strbuf *sp1;
	struct strbuf *sp2;
	struct strbuf *sp3;
	struct strbuf *sp4 = xmalloc(sizeof(struct strbuf));
	struct strbuf *sp5 = xmalloc(sizeof(struct strbuf));
	struct strbuf *sp6 = xmalloc(sizeof(struct strbuf));
	struct strbuf *sp7;

	strbuf_init(&sb5, 0);
	strbuf_init(sp1, 0);
	strbuf_init(sp2, 0);
	strbuf_init(sp3, 0);
	strbuf_init(sp4, 0);
	strbuf_init(sp5, 0);
	strbuf_init(sp6, 0);
	strbuf_init(sp7, 0);
	sp7 = xmalloc(sizeof(struct strbuf));

	use_before(&sb3);
	use_as_str("%s", sb7.buf);
	use_as_str("%s", sp1->buf);
	use_as_str("%s", sp6->buf);
	pass_pp(&sp3);

	strbuf_release(&sb1);
	strbuf_reset(&sb2);
	strbuf_release(&sb3);
	strbuf_release(&sb4);
	strbuf_release(&sb5);
	strbuf_release(&sb6);
	strbuf_release(&sb7);
	strbuf_release(sp1);
	strbuf_release(sp2);
	strbuf_release(sp3);
	strbuf_release(sp4);
	strbuf_release(sp5);
	strbuf_release(sp6);
	strbuf_release(sp7);

	use_after(&sb4);

	if (when_strict())
		return;
	strbuf_release(&sb8);
}

void test_other(void)
{
	struct string_list l = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;

	string_list_clear(&l, 0);
	string_list_clear(&sb, 0);
}

void test_worktrees(void)
{
	struct worktree **w1 = get_worktrees();
	struct worktree **w2 = get_worktrees();
	struct worktree **w3;
	struct worktree **w4;

	w3 = get_worktrees();
	w4 = get_worktrees();

	use_it(w4);

	free_worktrees(w1);
	free_worktrees(w2);
	free_worktrees(w3);
	free_worktrees(w4);
}
