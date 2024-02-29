/*
*
*	Generate some random lines for use in a2p1.
*
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]){
	
	FILE *myFile = fopen("./myFile", "w");

	for (int i = 0; i < 1000; i++){
		int randy = rand();
		fprintf(myFile, "Line %d: %d\n", i+1, randy);
	}

	fclose(myFile);

}
