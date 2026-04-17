#include <stdio.h>

int main() {
	__asm__ volatile (
		"mzero m0\n\t"
	);

	printf("Success\n");
	return 0;
}
