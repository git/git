#include <stdlib.h>
#include <stdio.h>

int func(int num);
void print_int(int num);
void print_ln();

int main() {
	int i;

	for (i = 0; i < 10; i++) {
		print_int(func(i));
	}

	print_ln();

	return 0;
}

int func(int num) {
	return num * num;
}

void print_int(int num) {
	printf("%d", num);
}

void print_ln() {
	printf("\n");
}

