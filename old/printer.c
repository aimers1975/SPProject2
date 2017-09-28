#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char **argv) {
	
	int i=0;
	printf("\ncmdline args count is %d", argc);

    printf("\nexe name=%s", argv[0]);

    for(int i=1; i< argc; i++) {
	    printf("\narg%d=%s", i, argv[i]);
	}

	printf("\n");

	while(1) {
		sleep(5);
		printf("printing again\n");
	}
	return 0;
}