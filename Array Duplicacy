#include <stdio.h>
int main() {
   int array[1000] ;
   int i,n,j,k;
    printf("Input the number of elements to store in the array :");
   scanf("%d",&n);
   for(i=0;i<n;i++)
      {
	  printf("element - %d : ",i);
	  scanf("%d",&array[i]);
	  }
      for ( i = 0; i < n; i++)
      {
        for (j = i+1; j < n;j++)
        {
         if (array[j]==array[i])
         {
            for ( k = j; k < n; k++)
            {
               array[k]=array[k+1];
            }
             j--;
            n--;  
         }  
      }
      }
      printf("Original content is\n");
       for(i = 0; i < n; i++)
    {
        printf("%d ", array[i]);
    }
    return 0;
}


      
