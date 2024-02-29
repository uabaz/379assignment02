/*
*   a2p2 - For CMPUT 379 Winter 2024 by Kyle Zwarich

    This program can be started as a "server":
        ./a2p2 -s

    This program can be started as a "client" with an inputFile "file":
        ./a2p2 -c file

    This program requires two system FIFO file descriptors in the working directory:
        ./fifo-0-1
        ./fifo-1-0

    If invoked as -s, the idNumber is 0; If invoked as -c, the idNumber is 1;

    This program imitates a "file-sharing, client/server interaction."
    An inputFile "file" has the following features:
        * a '#' at the beginning of the line flags the line as a comment and is skipped;
        * a '\n' at the beginning of the line indicates an empty line;
        * a line that starts with a character 'idNumber' begins an instruction.
    
    Instructions include the following:
        ##put/get/delete command
        "idNumber (put | get | delete) objectName"
            -the client with idNumber sends the server a put, get, or delete request
            -an object name has MAXWORD = 32 characters.

        ##gtime command
        "idNumber gtime"
            -the client with idNumber sends the server a "get time" request
        
        ##delay x command
        "idNumber delay x"
            -the client with idNumber delays reading and processing inputFile for
                x milliseconds
        
        ##quit command
        "idNumber quit"
            -the client with idNumber terminates normally.

    The server has the following duties:
        * stores an "object" table that can store up to 16 "objects";
            NOBJECT = 16
        * updates the "object" table as required by clients;
        * sends an "object" to client (if the object exists);
        * reports errors if any problems occur;
*/

//
//feature test macros (if needed)
//

//
//header includes
//
#include <stdio.h> //I/O functionality
#include <stdlib.h> //basic library functions
#include <string.h> //various useful string functions
#include <stddef.h> // size_t
#include <sys/types.h> // ssize_t
#include <sys/stat.h> //mkfifo
#include <ctype.h> // isdigit() functions
#include <error.h> //error reporting
#include <assert.h> //assert functionality
#include <fcntl.h> //file descriptor fcntl etc.
#include <unistd.h> //standard macros
#include <poll.h> //poll function for File Descriptor monitoring
#include <errno.h> //errno defs

//
//macros
//
#define NOBJECT 16 //maximum objects in server table;
#define MAXWORD 32 //maximum length of an object name;
#define MAXBLOCKLINES 3 //max number of lines in a transaction file within a file block
#define MAXLINELENGTH 80 //max number of characters in a file block line
#define MAXLINE 256 //used for tokenizer to handle full lines
#define MAX_NTOKENS 5 //used for tokenizer to handle command splits

//
//function/user struct definitions
//
void *serverInitTable(void *args);

//functions for all client/server communications
void *serverPut(void *args);
void *serverGet(void *args);
void *serverDelete(void *args);
void *serverReportTime(void *args);
void *clientPut(void *args);
void *clientGet(void *args);
void *clientDelete(void *args);
void *clientDelay(void *args);
void *clientQuit(void *args);
int clientRequestID(int fdC, int fdS);
void *testObject(void *args);
int Tokenizer(char inputStr[], char tokens[][MAXWORD], char seps[], char* pointerArray[]);

typedef enum KIND {get, put, delete, gtime, delay, reqid, ack, done, quit} KIND;

typedef struct intMsg {
    int clientID;
    KIND kind;
    int argument;
} intMsg;

typedef struct strMsg {
    char data1[MAXLINELENGTH];
    char data2[MAXLINELENGTH];
    char data3[MAXLINELENGTH];
} strMsg;

typedef struct sObject {
    int owner;
    char name[MAXWORD]; 
    strMsg package;
} sObject;

typedef union {intMsg mInt; strMsg mStr; sObject mObj;} DATA;
typedef struct {KIND kind; DATA data;} FRAME;
char commandList[][MAXWORD] = {"get", "put", "delete", "gtime", "delay", "reqid", "ack", "done", "quit"};



DATA packIntM(int clientID, KIND kind, int argument);
DATA packStrM(const char *a, const char *b, const char *c);
DATA packData(int ID, char name[], strMsg package);
void printFrame(const char *userPrefix, FRAME *frame);
void printObjectPacket(sObject obj);
FRAME receiveFrame(int fileDesc);
void sendFrame(int fileDesc, KIND kind, DATA *data);
KIND getFrameKind(char command[]);

//
//main function
//
int main(int argc, char* argv[]){

    //possible arguments
    //-s: server mode.
    //-c inputFile: client mode, requires a path to a transaction list "inputFile"

    //setup some globals
    char userFlag[3];                           //argv[1] duplicate
    assert(strlen(argv[1]) <= 2);               //ensure argv[1] is appropriate
    strncpy(userFlag, argv[1], 2);              //do a copy up to two char
    userFlag[2] = '\0';                         //replace newline with null-terminator
    char serverFlag[] = {'-','s', '\0'};        //set up server flag comparison string
    char clientFlag[] = {'-', 'c', '\0'};       //       client flag comp. str.

    //setup FIFOS:
    char fifoStoC[] = "./fifo-0-1";
    char fifoCtoS[] = "./fifo-1-0";

    //
    //  Run in Server Mode
    //
    if ((strcmp(userFlag, serverFlag) == 0)){

        int idNumber = 0;
        //create object table:
        sObject objectTable[NOBJECT];
            serverInitTable(objectTable);

        int servFD = open(fifoStoC, O_NONBLOCK, O_WRONLY);    //server pipe in non-blocking mode for writing
        if (servFD == -1){
            printf("Error opening [%s]: %s.\n", fifoStoC, strerror(errno));
        } else printf("server fd %d opened.\n", servFD);
        int cliFD = open(fifoCtoS, O_NONBLOCK, O_RDONLY);     //client pipe in non-blocking mode for reading
        if (cliFD == -1){
            printf("Error opening [%s]: %s.\n", fifoCtoS, strerror(errno));
        } else printf("client fd %d opened.\n", cliFD);

		//manage clients:
		int clientList[3];
			memset(&clientList, 0, sizeof(clientList));
		FRAME buffFrame;
			memset(&buffFrame, 0, sizeof(buffFrame));

        int hasQuit = 0;

        //poll? select? the fifo and wait for commands:
        nfds_t servFDNum = 1;
        struct pollfd servFDs[1];
        servFDs[0].fd = servFD;
        servFDs[0].events = POLLOUT; //is server fifo ready for writing? blocks, if not.

        struct pollfd cliFDs[3];
        nfds_t cliFDNum = 3;
        cliFDs[0].fd = cliFD;
        cliFDs[0].events = POLLIN; //does client fifo have any data inside?
        int ttl = 1000;

        while (!hasQuit){

            //repeatedly poll the server fifo for readiness;
            printf("Polling server fds for %d.%d sec.\n", ttl/1000, ttl%1000);
            int sretval = 0;
            sretval = poll(servFDs, servFDNum, ttl);

            if(sretval > 0){
                for (int i = 0; i < servFDNum; i++){
                    printf("%d\n", servFDs[i].revents);
                }
            }
            else if (sretval < 0){
                printf("server poll Error: %s.\n", strerror(errno));
            }

            //poll with a timeout of 2.5 sec on all client fifos for data;
            printf("Polling client fds for %d.%d sec.\n", ttl/1000, ttl%1000);
            int cretval = 0;
            cretval = 0;
            cretval = poll(cliFDs, cliFDNum, ttl);
            
            if(cretval > 0){
                //got some data, which fds have things?
                for (int i = 0; i < cliFDNum; i++){
                    if (cliFDs[i].revents != 0){
                        //this fd has some data; read it.
                        int nread = 0;
                        FRAME newFrame;
                        newFrame = receiveFrame(cliFDs[i].fd);
                    }
                }
            }
            else if ( cretval < 0){
                printf("client poll Error: %s.\n", strerror(errno));
            }
		
        }
        //testObject(&objectTable[2]);
        /*printf("NAME: [%s]\nDATA1: [%s]\nDATA2: [%s]\nDATA3: [%s]\n", 
        objectTable[2].name,
        objectTable[2].data1,
        objectTable[2].data2,
        objectTable[2].data3
        );*/

    }
    //
    //  Run in Client Mode
    //
    else if ((strcmp(userFlag, clientFlag) == 0)){

        //set up the FIFO pipes
        int cliFD = open(fifoCtoS, O_WRONLY);     //write to client pipe
        int servFD = open(fifoStoC, O_RDONLY);    //read from server pipe

        //run in client mode; open an instructions file
        FILE *clientData = fopen(argv[2], "r");

        //read from file one line at a time
        //each line with # = comment;
        //each '\n' skipped, 
        //each integer starts a command, 
        //each {, }, is a packet block indicator

        //some array setups for Tokenizer
        char tokens[3][MAXWORD];
            memset(tokens, 0, sizeof(tokens));
        char* tokenPointers[3];
            memset(tokenPointers, 0, sizeof(tokenPointers));
        char command[3][MAXWORD];
            memset(command, 0, sizeof(command));
        char seps[] = {'\n', ' ', '\t', '\0'};

        //set up items for getline() function
        char *currLine = NULL;
        int charsRead = 0;
        size_t len = 0;
        ssize_t nread = 0;

        //for part3; ask server for a client id
        int workclientID = 1;       //default to 1 if solo client (for part 2);
        int requestID = clientRequestID(cliFD, servFD);      //request ID from server if multi-client (for part 3);

        //data block flag for reading
        int isInBlock = 0;
        //objectName
        char objectName[MAXWORD];
            memset(objectName, 0, sizeof(objectName));

        while((nread = getline(&currLine, &len, clientData) != -1)){

            switch (currLine[0]){
                case '\n':          //skip empty newline
                    break;
                case '#':           //comment, skip
                    break;
                case '{':           //start of a block.
                    isInBlock = 1;  
                    break;
                case '}':           //end of a block.
                    isInBlock = 0;
                    break;
                default:            //if we get here, 'currLine' starts with some kind of character

                    // currLine starts with a number (client ID) but is not inside a block yet;
                    if (isdigit(currLine[0]) && isInBlock == 0) {

                        workclientID = strtol(currLine, NULL, 10); //grab the ID
                        Tokenizer(currLine, tokens, seps, tokenPointers);       //tokenize command
                        KIND checkType = getFrameKind(tokens[1]);        //decide what command
                        strncpy(objectName, tokens[2], MAXWORD);    //grab the object name

                        FRAME thisFrame;
                            memset(&thisFrame, 0, sizeof(thisFrame));
                        
                        DATA package;
                            memset(&package, 0, sizeof(package));

                        switch (checkType)
                        {
                        case put:
                            thisFrame.kind = put;
                            //grab the next few lines as a block to pack up;
                            thisFrame.data = packData(workclientID, objectName, package.mStr);
                            break;
                        case get:
                            thisFrame.kind = get;
                            thisFrame.data = packData(workclientID, objectName, package.mStr);
                            break;
                        case delete:
                            thisFrame.kind = delete;
                            thisFrame.data = packData(workclientID, objectName, package.mStr);
                            break;
                        case gtime:
                            thisFrame.kind = gtime;
                            thisFrame.data = packData(workclientID, objectName, package.mStr);
                            break;
                        case delay:
                            thisFrame.kind = delay;
                            int millisec = strtol(tokens[2], NULL, 10);
                            thisFrame.data = packIntM(workclientID, delay, millisec);
                            break;
                        case quit:
                            thisFrame.kind = quit;
                            thisFrame.data = packIntM(workclientID, quit, 0);
                            break;
                        default:
                            break;
                        }
                        //do stuff with thisFrame
                        printFrame("client", &thisFrame);
                    }
                    else if (currLine[0] != '}' && isInBlock == 1) {
                        //do NOT tokenize these: this is raw data being fed thru
                        int blockCounter = 0;
                        char structDataArray[3][MAXLINELENGTH];
                            memset(structDataArray, 0, sizeof(structDataArray));

                        while (currLine[0] != '}' && blockCounter < MAXBLOCKLINES) {
                            //add data to struct member up to 80 char
                            strncpy(structDataArray[blockCounter], currLine, MAXLINELENGTH);
                            nread = getline(&currLine, &len, clientData);
                            //counter ++
                            blockCounter ++;
                        }

                        //got the nice juicy data; need to package it.
                        DATA dataBlock = packStrM(structDataArray[0], structDataArray[1], structDataArray[2]);
                        DATA thisData = packData(workclientID, objectName, dataBlock.mStr);
                        //do stuff with thisData
                        //sendDataPacket();
                        //reinitialize for next block
                        memset(objectName, 0, sizeof(objectName));
                        workclientID = 1;
                        blockCounter = 0;
                        isInBlock = 0;
                    }

                    else {
                        printf("Error with input command file. Invalid client command");
                        exit(EXIT_FAILURE);
                    }

            }
        } 

        //finished reading the input file; should never get here because the client should
        //send a quit message which will exit
    
    }

    //end of server/client functionality. exit main function.

    return 0;
}

//
//other functions
//

/**
 * serverInitTable
 *  *arg[0]: a pointer to an array of serverObject structs
 * 
 *  *Initializes the memory at the provided array pointer
*/
void *serverInitTable(void *args){
    sObject *serverTable = args;
        memset(&serverTable, 0, sizeof(serverTable));
    printf("Server table initialized.\n");

    return NULL;
}

/**
 * testObject
 *  *arg[0]: a pointer to a specific serverObject 
 * 
 *  *Fills a test serverObject using hardcoded strings
*/
void *testObject(void *args){
    sObject *testObj = args;
    testObj->owner = 1;
    strcpy(testObj->name, "TestName");

    return NULL;
}

int Tokenizer(char inputStr[], char tokens[MAX_NTOKENS][MAXWORD], char separators[], char* pointerArray[]){
	//read the string at inputString;
	//break out the tokens into provided tokens array, 
	//	(based on separators);
	//add pointers to each token into pointerArray;
	//return the token count;	
	
	// initialize variables:
	int count;
	char *tokenPointer, inputStrCopy[MAXLINE];
	
	count = 0;
	
	memset(inputStrCopy, 0, sizeof(inputStrCopy));

	//clear output array;
	for (int i = 0; i <= MAX_NTOKENS; i++) {
			memset(tokens[i], 0, sizeof(tokens[i]));
		}	

	//backup the passed string
	strcpy(inputStrCopy, inputStr);

	//***Start to tokenise using strtok()
	if ((tokenPointer = strtok(inputStr, separators)) == NULL){
		return 0; //no tokens found
	}

	//first token copied here;
	strcpy(tokens[count], tokenPointer);
	//printf("Tokenizer results: [%s]", tokenPointer);
	pointerArray[count] = &tokens[count];
	count++;

	//walk through other tokens in the string
	while((tokenPointer = strtok(NULL, separators)) != NULL)
	{
		strcpy(tokens[count], tokenPointer);
		//printf("[%s]", tokenPointer);
		pointerArray[count] = &tokens[count];
		count++;
	}
	//printf("\n");

	//restore input String;
	strcpy(inputStr, inputStrCopy);

	//done working.
	return count;
}

/**
 * getFrameKind
 * 
 * converts a provided string into the proper command from the enum KIND
 * 
 * based on code fragment from
 *   https://stackoverflow.com/a/16844938
*/
KIND getFrameKind(char *whatKind){

    struct convertTable{
        KIND kind;
        char *str;
    };

    struct convertTable doConv[] = {
        {get, "get"},           //0...
        {put, "put"},
        {delete, "delete"},
        {gtime, "gtime"},
        {delay, "delay"},
        {reqid, "reqid"},
        {ack, "ack"},
        {done, "done"},
        {quit, "quit"},         //...8
    };

    KIND result = -1;

    for (int i = 0; i < sizeof(doConv)/sizeof(doConv[0]); i++){
        if (!strcmp(whatKind, doConv[i].str)){
            result = doConv[i].kind;            
            return result;
        }
    }
    //didn't find
    printf("String not found in KIND enum table.\n");
    result = -1;

    return result;
}

/**
 * packIntM:
 * 
 * Takes up to 3 integers and packages them into a struct for delivery in a frame.
 * Returns a intMsg struct for use
 * 
 * int clientID: id of client to respond to;
 * (int) KIND kind: an enum representing the command type;
 * int argument;
*/
DATA packIntM(int clientID, KIND kind, int argument){

    DATA package;
    memset(&package, 0, sizeof(DATA));

    package.mInt.clientID = clientID;
    package.mInt.kind = kind;
    package.mInt.argument = argument;

    return package;
}

/**
 * packStrM:
 * 
 * Takes up to 3 strings and packages them into a struct for delivery in a frame.
 * Returns a DATA struct for use
 * 
*/
DATA packStrM(const char *line1, const char *line2, const char *line3){

    DATA package;
    memset(&package, 0, sizeof(DATA));

    strncpy(package.mStr.data1, line1, MAXLINELENGTH);
    strncpy(package.mStr.data2, line2, MAXLINELENGTH);
    strncpy(package.mStr.data3, line3, MAXLINELENGTH);

    return package;
}
/**
 * packData:
 * 
 * Takes up to three data lines and packages them into a struct.
 * 
 * Returns a DATA struct. Used to store data blocks in the server's ObjectTable.
 * 
 * int ID = client id (aka 'owner');
 * char[] name = string specifying a unique object (aka 'key');
 * strMsg package = data to send;
 * 
*/
DATA packData(int ID, char name[], strMsg package){
    DATA object;
    memset(&object, 0, sizeof(DATA));

    object.mObj.owner = ID;
    strncpy(object.mObj.name, name, MAXWORD);
    object.mObj.package = package;

    return object;
}

/**
 * printFrame:
 * 
 * based on the "kind" of frame, do a print
 * of the content in commandPacket.
 * 
*/
void printFrame(const char *userPrefix, FRAME *frame){
    
    DATA data = frame->data;

    printf("%s [%s]>>", userPrefix, commandList[frame->kind]);

    switch (frame->kind)
    {
    case get:
        printf("[%d, %s]", data.mObj.owner, data.mObj.name);
        break;
    
    case put:
        printf("[%d, %s]", data.mObj.owner, data.mObj.name);
        break;
    
    case delete:
        printf("[%d, %s]", data.mObj.owner, data.mObj.name);
        break;
    
    case gtime:
        printf("[%d]", data.mInt.clientID);
        break;
    
    case delay:
        printf("[%d, %d]", data.mInt.clientID, data.mInt.argument);
        break;
    
    case reqid:
        printf("[%d, %d]", data.mInt.clientID, data.mInt.argument);
        break;
    
    case ack:
        break;
    
    case done:
        printf("%d", data.mInt.clientID);
        break;
    
    case quit:
        printf("%d", data.mInt.clientID);
        break;

    default:
        printf("UNKNOWN KIND: %d\n", frame->kind);
        break;
    }
    printf("\n");
}

/**
 * sendFrame
 * 
 * Utilize a two-unit frame to easily send, receive, and determine types
 * of commands sent via FIFO
 * 
 * int fd: file descriptor to send across
 * KIND kind: type of message (see enum)
 * DATA *data: pointer to a data package (intMsg, strMsg, etc.)
 * 
*/
void sendFrame (int fd, KIND kind, DATA *data){
    FRAME send;
        memset((char *) &send, 0, sizeof(send));
    send.kind = kind;
    send.data = *data;
    write(fd, (char *) &send, sizeof(send));
}

/**
 * receiveFrame
 * 
 * Unpack a two-unit frame from a FIFO fd:
 * 
 * int fd: file descriptor to read from
*/
FRAME receiveFrame (int fd) {
    int frmLen = 0;
    FRAME recFrame;
        memset(&recFrame, 0, sizeof(recFrame));
    frmLen = read(fd, (char *) &recFrame, sizeof(recFrame));
    if (frmLen != sizeof(recFrame)){
        printf("Received frame has len: [%d] but expected len: [%lu].\n", frmLen, sizeof(recFrame));
    }
    return recFrame;
}

/**
 *clientRequestID 
 * 
 * Send the server a query to see if there is space for another client;
 * if there is space, changes result to a client ID; otherwise -1 for no
 * slots available.
 * 
 * int fifoFD: a Client-to-Server FIFO that is already open and ready for writing
 * inf fifoS: a Server-to-Client FIFO that is already open and ready for reading
*/
int clientRequestID(int fifoC, int fifoS){
    int clientID = 1; //default id of 1 for single-client mode
    int asker = rand();

    //package data:
    DATA reqID = packIntM(asker, reqid, 0);
    //generate a frame
    sendFrame(fifoS, reqid, &reqID);

    return clientID;
}
