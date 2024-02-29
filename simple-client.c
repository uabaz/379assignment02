#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define NOBJECT 16 //maximum objects in server table;
#define MAXWORD 32 //maximum length of an object name;
#define MAXBLOCKLINES 3 //max number of lines in a transaction file within a file block
#define MAXLINELENGTH 80 //max number of characters in a file block line
#define MAXLINE 256 //used for tokenizer to handle full lines
#define MAX_NTOKENS 5 //used for tokenizer to handle command splits

int main (int argc, char* argv[]){

	int fd = open("./fifo-1-0", O_WRONLY);
	if (fd < 0){
		printf("Error could not open file.\n");
	}
	else{
		printf("Opened fd %d for writing.\n", fd);
	}

	struct mINT {
	int a;
	int b;
	int c;
	};

	struct mSTR {
		char data1[MAXLINELENGTH];
		char data2[MAXLINELENGTH];
		char data3[MAXLINELENGTH];
	};

	struct mOBJ {
		int owner;
		char name[MAXWORD]; 
		struct mSTR package;
	};

	union PACKAGE {
		struct mINT iData;
		struct mSTR sData;
		struct mOBJ oData;
	};

	struct DATA { 
		int TYPE;
		union PACKAGE package;
	};

	struct FRAME {
		int KIND;
		struct DATA data;
	};

	struct mINT intSt;
	memset(&intSt, 0, sizeof(intSt));
	struct FRAME myFrame;
	memset(&myFrame, 0, sizeof(myFrame));
	struct DATA myData;
	memset(&myData, 0, sizeof(myData));

	intSt.a = 1;
	intSt.b = 4000;
	intSt.c = 0;
	myData.TYPE = 0;
	myData.package.iData = intSt;
	myFrame.KIND = 4;
	myFrame.data = myData;

	int nwrote = 0;
	printf("FRAME with following:\n\
COMMAND:\t %d\n\
DATA>\t\n\
PKG-TYPE:\t %d\n\
PKG_1:\t%d\n\
PKG_2:\t%d\n\
PKG_3:\t%d\n", myFrame.KIND, myFrame.data.TYPE, myFrame.data.package.iData.a, myFrame.data.package.iData.b, myFrame.data.package.iData.c);

	nwrote = write(fd, &myFrame, sizeof(myFrame));
	if (nwrote == -1){
		printf("Err. did not write. %s\n", strerror(errno));
	}
	if (nwrote < sizeof(myFrame)){
		printf("Error: didn't write.\n");
	}
	else{
		printf("Wrote %d to fifo.\n", nwrote);
	}

	close(fd);

	return 0;
}
