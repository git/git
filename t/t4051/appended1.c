
int appended(void) // Begin of first part
{
	int i;
	char *s = "a string";

	printf("%s\n", s);

	for (i = 99;
	     i >= 0;
	     i--) {
		printf("%d bottles of beer on the wall\n", i);
	}

	printf("End of first part\n");
