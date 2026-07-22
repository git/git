int use_FREE_AND_NULL(int *v)
{
	free(*v);
	*v = NULL;
}

int need_no_if(int *v)
{
	if (v)
		free(v);
}
