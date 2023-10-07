#include<stdio.h>
#include<stdlib.h>

void add(float a, float b);
void substract(float a, float b);
void multiply(float a, float b);
void devide(float a, float b);

int main(){
	float num1;
	float num2;
	int choice;

	printf("\nEnter 1 for Addition\nEnter 2 for Substraction\nEnter 3 for Multiplication\nEnter 4 for Division\n");

	printf("\nEnter your choice : ");
	scanf("%d",&choice);

   printf("enter number1 : ");
   scanf("%f",&num1);

	printf("enter number2 : ");
	scanf("%f",&num2);

	switch(choice){
		case 1:
			add(num1,num2);
			break;
		case 2:
			substract(num1,num2);
			break;
		case 3:
			multiply(num1,num2);
			break;
		case 4:
			devide(num1,num2);
			break;
		default: {
			printf("invalid input!\n");
		}	
      return 0;
      }
}
void add(float a,float b) {
	printf("\nResult: %f",a+b);	
}
void substract(float a,float b){
	printf("\nResult: %f",a-b);
}
void multiply(float a,float b){
	printf("\nResult: %f",a*b);
}
void devide(float a,float b){
     if(b==0){
     	printf("\nCan not divide by zero!");
	 }else{
	 	printf("\nResult: %f",a/b);
	 }
}
