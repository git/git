int use_FREE_AND_NULL(int *v)
{
	FREE_AND_NULL(*v);
}

int need_no_if(int *v)
{
	free(v);
}
