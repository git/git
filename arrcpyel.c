#include<stdio.h>
#include<conio.h>
int main()
{
	clrscr();
	int i;
	int arr[5];
	int arr2[5];

	printf("Enter The Number:\n");
	for(i=0; i<5; i++)
	{
		scanf("%d",&arr[i]);
	}

	printf("First Array Elements:\n");
	for(i=0; i<5; i++)
	{
		printf("%d\n",arr[i]);
	}

	printf("Content Copy\n");
	for(i=4; i>=0; i--)
	{
		arr2[i]=arr[4-i];
	}
	printf("Second Array In Reverse Order:\n");
	for(i=0; i<5; i++)
	{
		printf("%d\n",arr2[i]);
	}
	getch();
	return 0;
}
