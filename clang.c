#include <stdio.h>
int main()
{
    int n;
    printf("enter number : ");
    scanf("%d", &n);

    int sum = 0;
    for (int i=1; i<=n; i++)
    {
        sum = sum + i; // sum += i
    }

    printf("sum is %d \n", sum);

    for (int i=n; i>=1; i--)
    {
        printf("%d \n", i);
    }

    return 0;
}
