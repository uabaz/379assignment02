/*
* Assignment 2: a2p1
*	by Kyle Zwarich for CMPUT 379 Assignment 2
*
*	Usage:
*		'a2p1 nLine inputFile delay'
*	nLine: integer -- 
			how many text lines to read
*	inputFile: string --
			file path to the file to read (real file on disk)
*	delay: integer --
			number of milliseconds to delay (converted internally)

*	This program reads from some inputFile,
	a number of lines nLine, and prints them to
	STDOUT. It then waits for some delay in milliseconds,
	during which the user can enter commands.

*	Commands include the keyword 'quit' to 
	terminate the program and return to the terminal.

*	Otherwise, the entered command is sent to a 'popen()' function
	and interpreted by the shell.

*	The program iterates indefinitely until the 'quit' command is issued.

*/

#define _GNU_SOURCE 1 // add support for some POSIX related commands

//
//libary includes:
//
#include <stdio.h>	//in/out and files, popen()
#include <errno.h>	//errors
#include <stdlib.h> //most functions
#include <string.h> //strtol()
#include <unistd.h> //pause()
#include <pthread.h> //pthread support
#include <signal.h> //sigaction()

//
//macro definitions:
//
#define MAXFD 128 //maximum file descriptor length
#define MAXCOMMAND 32 //maximum command str length
#define MAXLINE 255 //maximum line length

//
//user-created structs:
//
struct printLineArgs {FILE *fd; fpos_t *startpos; int nlines;};
struct threadArgs {int delayTime; pthread_t threadID; struct printLineArgs printArgs;};

//
//function declarations
//
void *makeTimer(void *arg);
void *printLinesFromFile(void *arg);
void *onAlarmSignal(void *arg);

//
//main function
//
int main(int argc, char* argv[]){
	
	//process arguments:
	//0: program name;
	//1: number of lines to read;
	//2: fileDescriptor to read;
	//3: delay in milliseconds

	//process arg1
	int nLines;
	char *nLinesEnd;
	if ((nLines = strtol(argv[1], &nLinesEnd, 10)) == 0){
		printf("a2p1 int:_1_ str:_2_ int:_3_; arg 1 is not a valid, non-zero integer: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	//process arg2
	char myFD[MAXFD];
	memset(myFD, 0, sizeof(myFD));
	strcpy(myFD, argv[2]);

	//process arg3
	int delayTime;
	char *delayEnd;
	if ((delayTime = strtol(argv[3], &delayEnd, 10)) == 0) {
		printf("a2p1 int:_1_ str:_2_ int:_3_; arg 3 is not a valid, non-zero integer: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

		//setup the quit command string;
	char qCommand[] = "quit";

		//open the file from arg2 for reading:
	FILE *myFile = fopen(myFD, "r");
	fpos_t fileStartPos;
	fileStartPos.__pos = 0;
	fpos_t fileCurrPos;
	memset(&fileCurrPos, 0, sizeof(fileCurrPos));

		//create a struct for dealing with printing
	struct printLineArgs plArgs;
	plArgs.fd = myFile;
	plArgs.nlines = nLines;
	plArgs.startpos = &fileStartPos;

		//create a string buffer to hold commands;
	char currCommand[MAXCOMMAND];
	memset(currCommand, 0, sizeof(currCommand));

		//signal blocking for main thread; don't want to terminate when SIGALRM!
	sigset_t mySignalSet;
	sigemptyset(&mySignalSet);
	sigaddset(&mySignalSet, SIGALRM);
	pthread_sigmask(SIG_BLOCK, &mySignalSet, NULL);

		//create a struct to pass information through functions
	struct threadArgs myArguments;
	myArguments.delayTime = delayTime;
	myArguments.threadID = pthread_self();
	myArguments.printArgs = plArgs;
	//printf("Parent thread is: [%ld]\n", myArguments.threadID);

		//start handler thread
	pthread_t myHandlerThread = 0;
	pthread_create(&myHandlerThread, NULL, &onAlarmSignal, &myArguments);
		//start alarm thread
	struct threadArgs handlerArgs;
	handlerArgs.delayTime = myArguments.delayTime;
	handlerArgs.threadID = myHandlerThread;
	handlerArgs.printArgs = plArgs;
	pthread_t myAlarmThread = 0;
	pthread_create(&myAlarmThread, NULL, &makeTimer, &handlerArgs);

	//
	//	Main loop: uses fgets() from stdin to get user commands.
	//			user must type 'quit' to exit loop.
	//			Command entry spans across delay; is constantly running
	//			on the main thread awaiting input.
	//
	int hasQuit = 0;
	while (!hasQuit) {

		char userInput[MAXCOMMAND];
		memset(&userInput, 0, sizeof(userInput));
		char *userIn = userInput;

		fgets(userIn, MAXCOMMAND, stdin);
		//process commands;
		strcpy(currCommand, userIn);
		currCommand[strlen(currCommand)-1] = '\0'; //eliminate newline char
		printf("\nCommand [%s] running;\n", currCommand);
		int strcmpRes = strcmp(currCommand, qCommand);
		//printf("String comparison result is : %d", strcmpRes);

		//command is 'quit'
		if (strcmpRes == 0){
			printf("Quit command entered. Stopping.\n");
			hasQuit = 1;
		}

		//command is anything else
		else {
			//use popen() to start a new process based on command entered
			FILE *myPipe = popen(currCommand, "r");
			char outputLine[MAXLINE];
			printf("Command output: \n");
			while((fgets(outputLine, 255, myPipe)) != NULL){
				printf("%s", outputLine);
			}
			pclose(myPipe);
			printf("Waiting for user command:\n");
		}
	}

	//close the file descriptor:
	fclose(myFile);
	return 0;
}

//
//other functions
//

/** onAlarmSignal()
 * This function sets up a handler for SIGALRM
 * 
 * arg[0] : threadArgs struct containing information about specified delay time, calling thread id,
 * 			and a struct of type printLineArgs with data passed through by caller
 *
*/
void *onAlarmSignal(void *arg){

	pthread_t alarmTID = pthread_self();
	//printf("\nThread started: onAlarmSignal [%ld]\n", alarmTID);
	sigset_t threadMask;
	sigemptyset(&threadMask);
	sigaddset(&threadMask, SIGALRM);
	//pthread_sigmask(SIG_BLOCK, &threadMask, NULL);

	//pass the arguments along
	struct threadArgs *passedArgs = arg;
	struct threadArgs alarmArgs;
	alarmArgs.delayTime = passedArgs->delayTime;
	alarmArgs.threadID = alarmTID;
	alarmArgs.printArgs = passedArgs->printArgs;
	struct printLineArgs printLines = passedArgs->printArgs;

	//wait for the SIGALRM signal 14, and spawn a new alarm handler if we get one
	pthread_t newThread = 0;
	int sigNo = 0;
	sigwait(&threadMask, &sigNo);
	printf("Signal received: [%d]\n", sigNo);
	pthread_create(&newThread, NULL, &onAlarmSignal, &alarmArgs);

	//print from a file
	printLinesFromFile(&printLines);

	//create a new timer
	pthread_t newTimer = 0;
	struct threadArgs timerArgs;
	timerArgs.delayTime = passedArgs->delayTime;
	timerArgs.threadID = newThread;
	pthread_create(&newTimer, NULL, &makeTimer, &timerArgs);

	return NULL;
}

/** makeTimer
*This function runs a timer as defined by the user on the command line.
*	Args defined in struct threadArgs:
*	threadArgs.delayTime (int): delay time in milliseconds
*	threadArgs.threadID (pthread_t): calling thread id
*/
void *makeTimer(void *arg){

	/**
	//initialize a sigset:
	sigset_t ignoreAlarm;
	sigemptyset(&ignoreAlarm);
	*/

	sigset_t blockedSignals;
	sigemptyset(&blockedSignals);
	sigaddset(&blockedSignals, SIGALRM);
	//add SIGALRM to blocked signals for this thread:
	pthread_sigmask(SIG_BLOCK, &blockedSignals, NULL);
	
	//for debugging:
	//pthread_t timertID = pthread_self();
	//printf("\nThread started: makeTimer [%ld]\n", timertID);

	struct threadArgs *myArg = arg;
	int time_ms = 0;
	time_ms = myArg->delayTime;
	pthread_t threadID = 0;
	threadID = myArg->threadID;

	struct timespec myTimer;
	myTimer.tv_nsec = (time_ms % 1000) * 1000000;
	myTimer.tv_sec = time_ms / 1000;
	printf("Delay timer started: [%ld.%3.3d] seconds.\n", myTimer.tv_sec, (time_ms % 1000));
	printf("Waiting for user command:\n");
	nanosleep(&myTimer, NULL);

	//delay done; fire a signal...
	printf("\nTimer expired. Firing SIGALRM to parent thread [%ld].\n", threadID);
	pthread_kill(threadID, SIGALRM);

	return((void *)0);
}


/** printLinesFromFile()
 * 
 * Prints lines from a file descriptor.
 * Pass through a struct pointer of type printLineArgs:
 * 	FILE *fd: pointer to a file on disk that has been previously opened in 'r' mode;
 * 	fpos_t *startpos: a pointer holding the position to start reading from the descriptor;
 * 	int nlines: number of lines to read from the descriptor.
*/
void *printLinesFromFile(void *arg){

	//get the passthru data
	struct printLineArgs *passedArgs = arg;
	FILE *fd = passedArgs->fd;
	fpos_t *startpos = passedArgs->startpos;
	int nlines = passedArgs->nlines;

	//set position based on passed data
	fsetpos(fd, startpos);
	for (int j = 0; j < nlines; j++){
		//print lines
		char currline[MAXLINE];
		memset(currline, 0, sizeof(currline));

		if (fgets(currline, sizeof currline, fd) != NULL){
			printf("%s", currline);
		}	
		else{
			printf("End of file reached.\n");
			rewind(fd);
		}
	}
	//set the new "start" 
	fgetpos(fd, startpos);

	return NULL;
}
