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
#include <unistd.h> //standard macros, usleep
#include <poll.h> //poll function for File Descriptor monitoring
#include <errno.h> //errno defs
#include <time.h> //timer
#include <sys/times.h> //times

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
typedef enum KIND {get, put, delete, gtime, delay, reqid, ack, done, quit, invalid, stime} KIND;
char commandList[][MAXWORD] = {"get", "put", "delete", "gtime", "delay", "reqid", "ack", "done", "quit", "invalid", "stime"};

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

typedef union { intMsg mInt; strMsg mStr; sObject mObj; } PACKAGE;
typedef struct DATA { int TYPE; PACKAGE package; } DATA;
typedef struct {KIND kind; DATA data;} FRAME;

//functions for all client/server communications
int clientRequestID(int fdC, int fdS);
void *testObject(void *args);
int Tokenizer(char inputStr[], char tokens[][MAXWORD], char seps[], char* pointerArray[]);
DATA packIntM(int clientID, KIND kind, int argument);
DATA packStrM(const char *a, const char *b, const char *c);
DATA packData(int ID, char name[], strMsg package);
void printFrame(const char *userPrefix, FRAME *frame);
void printObjectPacket(sObject obj);
FRAME receiveFrame(int fileDesc);
void sendFrame(int fileDesc, KIND kind, DATA *data);
KIND getFrameKind(char command[]);
int serverACK(int clientFD, KIND frameKind, int dataType);
FRAME initFrame();

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
    //single-server; single-client mode;
    char fifoStoC[] = "./fifo-0-1";
    char fifoCtoS[] = "./fifo-1-0";

    // ====================================================================================================
    //  Run in Server Mode
    // ====================================================================================================
    if ((strcmp(userFlag, serverFlag) == 0)){

        #define STAG "*[S]: "

        //setup a timer since server start.
        time_t startTime = time(NULL);

        int hasQuit = 0;
        int idNumber = 0;

        //create object table:
        sObject objectTable[NOBJECT];
        for(int i = 0; i < NOBJECT; i++) memset(&objectTable[i], 0, sizeof(sObject));
        int objectTableCapacity = 0;

        //open FIFO pipes:
        int cliFD = open(fifoCtoS, O_RDWR);     //client pipe in non-blocking mode
        if (cliFD == -1){
            printf(STAG "Error opening [%s]: %s.\n", fifoCtoS, strerror(errno));
        } else printf(STAG "open c|s fifo %s, %d\n", fifoCtoS, cliFD);

        int servFD = open(fifoStoC, O_RDWR);    //server pipe in non-blocking mode
        if (servFD < 0){
            printf(STAG "Error opening [%s]: %s.\n", fifoStoC, strerror(errno));
        } else printf(STAG "open s|c fifo %s, %d\n", fifoStoC, servFD);


		//manage clients (for a2p3);
		int clientList[3];
			memset(&clientList, 0, sizeof(clientList));

        /*
        nfds_t servFDNum = 1;
        struct pollfd servFDs[1];
        servFDs[0].fd = servFD;
        servFDs[0].events = POLLOUT; //is server fifo ready for writing? blocks, if not.
        */

        struct pollfd cliFDs[3];
        nfds_t cliFDNum = 3;
        cliFDs[0].fd = cliFD;
        cliFDs[0].events = POLLIN; //does any client fifo have data inside?
        
        //time to poll fifos
        int ttl = 2500;

        while (!hasQuit){

            //reset poll structs?
            /*
            for (int i = 0; i < servFDNum; i++){
                servFDs[i].events = POLLOUT;
                servFDs[i].revents = 0;
            }
            for (int i = 0; i < cliFDNum; i++){
                cliFDs[i].events = POLLIN;
                cliFDs[i].revents = 0;
            }
            */

            //printf("Polling client fds for %d.%d sec.\n", ttl/1000, ttl%1000);
            int cretval = 0;
            cretval = poll(cliFDs, cliFDNum, ttl);
            
            if(cretval > 0){
                //got some data, which fds have things?
                for (int i = 0; i < cliFDNum; i++){
                    if (cliFDs[i].revents != 0){
                        printf(STAG "fd %d with event %d.\n", cliFDs[i].fd, cliFDs[i].revents);
                        //writer has POLLHUP; reopen the fd.
                        //stops repeated data reads on fd's that keep reporting POLLHUP???
                        if (cliFDs[i].revents == 16){
                            close(cliFDs[i].fd);
                            open(fifoCtoS, O_RDWR);
                            break;
                        }

                        FRAME newFrame = initFrame();
                        newFrame = receiveFrame(cliFDs[i].fd);
                        printFrame(STAG "got client data from fd", &newFrame);

                        //send ack:
                        DATA ackData;
                        memset(&ackData, 0, sizeof(DATA));
                        ackData = packIntM(newFrame.data.TYPE, newFrame.kind, 0);
                        serverACK(servFD, newFrame.kind, 0);

                        //process client req's:
                        sObject cliObj;
                        memset(&cliObj, 0, sizeof(cliObj));

                        sObject servObj;
                        memset(&servObj, 0, sizeof(servObj));

                        // =======================================================================
                        // SERVER RESPONSES TO CLIENT REQUESTS
                        // =======================================================================
                        switch(newFrame.kind){

                            //
                            // PUT
                            //
                            case (put):;
                                cliObj = newFrame.data.package.mObj;
                                int objSlot;
                                
                                //check if table is full.
                                if(objectTableCapacity < NOBJECT){
                                    //table has space, check for entries
                                    for(int j = 0; j < NOBJECT; j++){
                                        if (strncmp(objectTable[j].name, cliObj.name, sizeof(objectTable[j].name)) == 0){
                                            printf(STAG "PUT error: item already exists. [%s]\n", cliObj.name);
                                            break;
                                        }
                                        //didn't find anything;
                                    }
                                }
                                else{
                                    printf(STAG "PUT error: server table capacity maxed [%d].\n", NOBJECT);
                                    break;
                                }

                                //check if table has an empty space.
                                sObject emptyObj;
                                memset(&emptyObj, 0, sizeof(sObject));

                                int index = -1;
                                for(int k = 0; k < NOBJECT; k++){
                                    if (memcmp(&emptyObj, &objectTable[k], sizeof(sObject)) == 0){
                                        index = k;
                                        // all good, copy data:
                                        objectTable[index] = cliObj;
                                        objectTableCapacity +=1;

                                        printf(STAG "PUT at loc [%d]:\n\
NAME: \t[%s]\n\
OWNR: \t[%d]\n\
LOAD: [%s], [%s], [%s]\n",
                                index,
                                &objectTable[index].name[0],
                                &objectTable[index].owner,
                                &objectTable[index].package.data1,
                                &objectTable[index].package.data2,
                                &objectTable[index].package.data3);
                                        break;
                                    }
                                
                                }
                                

                                break;

                            //
                            // GET
                            //
                            case (get):;
                                cliObj = newFrame.data.package.mObj;
                                for (int g = 0; g < objectTableCapacity; g++){
                                    //search for named obj in table:
                                    if (strncmp(objectTable[g].name, cliObj.name, sizeof(objectTable[g].name)) == 0){ 
                                        DATA foundData = packData(cliObj.owner, cliObj.name, cliObj.package);
                                        sendFrame(servFD, get, &foundData);
                                        break;
                                    }
                                }
                                //didn't find obj in table;
                                printf(STAG "GET error: object [%s] not found in server table.\n", cliObj.name);
                                break;

                            //
                            // DELETE
                            //
                            case (delete):;
                                cliObj = newFrame.data.package.mObj;
                                for (int d = 0; d < objectTableCapacity; d++){
                                    if (strncmp(objectTable[d].name, cliObj.name, sizeof(objectTable[d].name)) == 0){
                                        printf(STAG "deleting [%d]:[%s] from table; this is final!\n", d, cliObj.name);
                                        memset(&objectTable[d], 0, sizeof(sObject));
                                        objectTableCapacity -=1;
                                    }
                                }
                                //didn't find obj in table;
                                printf(STAG "DELETE error: [%s] not found in table. Could not delete.\n", cliObj.name);
                                break;

                            //
                            // GTIME
                            //
                            case (gtime):;
                                DATA timeData;
                                memset(&timeData, 0, sizeof(timeData));
                                time_t currTime = time(NULL);
                                time_t elapsed = currTime - startTime;
                                timeData = packIntM(0, 0, elapsed);
                                sendFrame(servFD, stime, &timeData);
                                printf(STAG "send elapsed time [%d sec.]\n", elapsed);
                                break;
                            
                            //
                            // DELAY
                            //
                            case (delay):;

                                break;
                            
                            //
                            // QUIT
                            //
                            case (quit):;
                                printf(STAG "client quit!");
                                break;

                            default:
                                break;
                        } // end of switch cases for server responses;
                    } // end of if statement for a POLLIN event;
                } // end of for loop of client descriptors
            } //end of if statement for cretval/poll
            else if (cretval == 0){
                printf("*[S]: Poll timeout.\n");
            }
            else if (cretval < 0){
                printf("*[S]: Poll error: %s.\n", strerror(errno));
            }
        } // end while loop
    }  // END SERVER MODE if-statement ====================================================================



    // ====================================================================================================
    //  Run in CLIENT Mode
    // ====================================================================================================
    else if ((strcmp(userFlag, clientFlag) == 0)){
        #define CTAG "*[C]: "
        //set up the FIFO pipes
        int cliFD = open(fifoCtoS, O_RDWR);     //write to pipe: client-to-server
            if (cliFD < 0){
                printf(CTAG "open c|s fd [%s] failed.\n", fifoCtoS);
            } else printf(CTAG "open c|s fd [%d]\n", cliFD);
        int servFD = open(fifoStoC, O_RDWR);    //read from server pipe
            if (servFD < 0){
                printf(CTAG "open s|c fd [%s] failed.\n", fifoStoC);
            } else printf(CTAG "open s|c fd [%d]\n", servFD);

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
        //objectName
        char objectName[MAXWORD];
            memset(objectName, 0, sizeof(objectName));

        //for part3; ask server for a client id
        int workclientID = 1;       //default to 1 if solo client (for part 2);
        //int requestID = clientRequestID(cliFD, servFD);      //request ID from server if multi-client (for part 3);

        //set up items for getline() function
        char *currLine = NULL;
        int charsRead = 0;
        size_t len = 0;
        ssize_t nread = 0;

        //reading the instruction set from clientData:
        while((nread = getline(&currLine, &len, clientData) != -1)){

            switch (currLine[0]){
                case '\n':          //skip empty newline
                    break;
                case '#':           //comment, skip
                    break;
                case '}':
                    break;
                default:            //if we get here, 'currLine' starts with some kind of character
                    // currLine starts with a number (client ID)
                    if (isdigit(currLine[0]) != 0){

                        Tokenizer(currLine, tokens, seps, tokenPointers);       //tokenize command
                        workclientID = strtol(tokens[1], NULL, 10);             //grab client id
                        KIND checkType = getFrameKind(tokens[1]);               //grab command type
                        strncpy(objectName, tokens[2], MAXWORD);                //grab the object name

                        FRAME thisFrame = initFrame();
                        FRAME gotAck = initFrame();
                        DATA payload;
                            memset(&payload, 0, sizeof(payload));

                        //What kind of command are we dealing with?
                        //Create a proper frame within each command type case.
                        switch (checkType){
                            case put:;
                                thisFrame.kind = put;
                                //grab the next few lines as a block to pack up;
                                //do NOT tokenize these: this is raw data being fed thru
                                int blockCounter = 0;
                                char structDataArray[3][MAXLINELENGTH];
                                    memset(structDataArray, 0, sizeof(structDataArray));

                                if ((nread=getline(&currLine, &len, clientData)) > 0){
                                    if (currLine[0] != '{'){
                                        printf(CTAG "PUT: bad data block (does file include a '{' marker after put command?).\n");
                                        break;
                                    }
                                }

                                while (blockCounter < MAXBLOCKLINES) {
                                    //add data to struct member up to 80 char
                                    nread = getline(&currLine, &len, clientData);
                                    if (currLine[0] =='}') break;
                                    strncpy(structDataArray[blockCounter], currLine, MAXLINELENGTH);
                                    blockCounter ++;
                                }

                                //got the nice juicy data; need to package it
                                payload = packStrM(structDataArray[0], structDataArray[1], structDataArray[2]);
                                thisFrame.data = packData(workclientID, objectName, payload.package.mStr);
                                //reinitialize for next block
                                memset(objectName, 0, sizeof(objectName));
                                workclientID = 1;
                                blockCounter = 0;
                                //do stuff with thisFrame
                                printFrame("c to s: ", &thisFrame);
                                sendFrame(cliFD, thisFrame.kind, &thisFrame.data);
                                //get ack
                                gotAck = receiveFrame(servFD);
                                printFrame("s msg: ", &gotAck);
                                break;

                            case get:;
                                thisFrame.kind = get;
                                thisFrame.data = packData(workclientID, objectName, payload.package.mStr);
                                //do stuff with thisFrame
                                printFrame("c to s", &thisFrame);
                                sendFrame(cliFD, thisFrame.kind, &thisFrame.data);
                                //get ack
                                gotAck = receiveFrame(servFD);
                                printFrame("s msg: ", &gotAck);
                                break;

                            case delete:
                                thisFrame.kind = delete;
                                thisFrame.data = packData(workclientID, objectName, payload.package.mStr);
                                printFrame("c to s", &thisFrame);
                                sendFrame(cliFD, thisFrame.kind, &thisFrame.data);
                                gotAck = receiveFrame(servFD);
                                printFrame("s msg: ", &gotAck);
                                break;

                            case gtime:
                                thisFrame.kind = gtime;
                                thisFrame.data = packData(workclientID, objectName, payload.package.mStr);
                                //do stuff with thisFrame
                                printFrame("c to s", &thisFrame);
                                sendFrame(cliFD, thisFrame.kind, &thisFrame.data);
                                //get ack
                                FRAME gotACK = receiveFrame(servFD);
                                printFrame("s msg: ", &gotACK);
                                //get time
                                FRAME gotTime = receiveFrame(servFD);
                                printFrame("SERVER UPTIME: ", &gotTime);
                                break;

                            case delay:
                                thisFrame.kind = delay;
                                int millisec = strtol(tokens[2], NULL, 10);
                                thisFrame.data = packIntM(workclientID, delay, millisec);
                                printf(CTAG "client command DELAY; sleeping for [%d.%.2d]s.\n", millisec/1000, millisec%1000);
                                usleep(millisec*1000);
                                break;

                            case quit:
                                thisFrame.kind = quit;
                                thisFrame.data = packIntM(workclientID, quit, 0);
                                break;

                            default:
                                break;
                        }
                    }
                    else {
                        printf(CTAG "Error with input command file. Invalid client command");
                        exit(EXIT_FAILURE);
                    }
                break;
            }
        }
        //finished reading the input file; should never get here because the client should
        //send a quit message which will exit
    
    } //END CLIENT MODE =====================================================================================
    
    //end of server/client functionality. exit main function.

    return 0;
}   //END MAIN FUNCTION =====================================================================================

//
//other functions
//

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
        {quit, "quit"},
        {invalid, "invalid"},
        {stime, "stime"},         //...9
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

    DATA mINT;
    memset(&mINT, 0, sizeof(DATA));

    mINT.TYPE = 0;
    mINT.package.mInt.clientID = clientID;
    mINT.package.mInt.kind = kind;
    mINT.package.mInt.argument = argument;

    return mINT;
}

/**
 * packStrM:
 * 
 * Takes up to 3 strings and packages them into a struct for delivery in a frame.
 * Returns a DATA struct for use
 * 
*/
DATA packStrM(const char *line1, const char *line2, const char *line3){

    DATA mSTR;
    memset(&mSTR, 0, sizeof(DATA));

    mSTR.TYPE = 1;
    strncpy(mSTR.package.mStr.data1, line1, MAXLINELENGTH);
    strncpy(mSTR.package.mStr.data2, line2, MAXLINELENGTH);
    strncpy(mSTR.package.mStr.data3, line3, MAXLINELENGTH);

    return mSTR;
}
/**
 * packData:
 * 
 * Takes up to three data lines and packages them into a struct.
 * 
 * Returns a DATA struct. Used to manipulate the server object table which
 * requires a DATA type of mObj and some ownership data.
 * 
 * int ID = client id (aka 'owner');
 * char[] name = string specifying a unique object (aka 'key');
 * strMsg package = data to send;
 * 
*/
DATA packData(int ID, char name[], strMsg package){
    DATA mOBJ;
    memset(&mOBJ, 0, sizeof(DATA));

    mOBJ.TYPE = 2;
    mOBJ.package.mObj.owner = ID;
    strncpy(mOBJ.package.mObj.name, name, MAXWORD);
    mOBJ.package.mObj.package = package;

    return mOBJ;
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
        printf("[[%d, %s]]", data.package.mObj.owner, data.package.mObj.name);
        break;
    
    case put:
        printf("[[%d, %s]]", data.package.mObj.owner, data.package.mObj.name);
        break;
    
    case delete:
        printf("[[%d, %s]]", data.package.mObj.owner, data.package.mObj.name);
        break;
    
    case gtime:
        printf("[[%d]]", data.package.mInt.clientID);
        break;
    
    case delay:
        printf("[[%d, %d]]", data.package.mInt.clientID, data.package.mInt.kind);
        break;
    
    case reqid:
        printf("[[%d, %d]]", data.package.mInt.clientID, data.package.mInt.argument);
        break;
    
    case ack:
        printf("[packagetype[%d], framekind[%d]]", data.package.mInt.clientID, data.package.mInt.kind);
        break;
    
    case done:
        printf("[%d]", data.package.mInt.clientID);
        break;
    
    case quit:
        printf("[%d]", data.package.mInt.clientID);
        break;

    case invalid:
        printf("Invalid FRAME; did you send strings or other data type?\n");
        break;
    
    case stime:
        printf("[%d seconds]", data.package.mInt.argument);
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
 * DATA *data: pointer to a data struct(type, iMSG/sMSG/oMSG)
 * 
*/
void sendFrame (int fd, KIND kind, DATA *data){
    FRAME send;
        memset((char *) &send, 0, sizeof(send));
    send.kind = kind;
    send.data.TYPE = data->TYPE;

    switch (data->TYPE)
    {
    case 0:
        //int-type payload
        send.data.package.mInt = data->package.mInt;
        break;
    case 1:
        send.data.package.mStr = data->package.mStr;
        break;
    case 2:
        send.data.package.mObj = data->package.mObj;
        break;
    default:
        break;
    }
    int nwrote = 0;
    nwrote = write(fd, (char *) &send, sizeof(send));
    if (nwrote < 0){
        printf("sendFrame error: %d wrote; %s on fd %d\n", nwrote, strerror(errno), fd);
    }
    else if (nwrote != sizeof(send)){
        printf("sendFrame error: %s\n", strerror(errno));
    };
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
        FRAME nullFrame;
        memset(&nullFrame, 0, sizeof(nullFrame));
        nullFrame.kind = invalid;
        return nullFrame;
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
 * int fifoC: a Client-to-Server FIFO that is already open and ready for writing
 * inf fifoS: a Server-to-Client FIFO that is already open and ready for reading
*/
int clientRequestID(int fifoC, int fifoS){
    int clientID = 1; //default id of 1 for single-client mode
    int asker = rand();

    //package data:
    DATA reqID = packIntM(clientID, reqid, asker);
    //generate a frame
    sendFrame(fifoC, reqid, &reqID);
    return clientID;
}

/**
 * serverACK
 * 
 * Send simple ack msg across a FIFO for printing on the other side.
 * int clientFD: fifo to sent msg
 * KIND frameKind: msg type recv'd
 * int dataType: type of data package in the frame client had sent
 * 
 * returns -1 if error, otherwise returns clientFD
*/
int serverACK(int servFD, KIND frameKind, int dataType){
    FRAME ackF;
    memset(&ackF, 0, sizeof(ack));
    ackF.kind = ack;
    ackF.data = packIntM(0, frameKind, dataType);
    int nwrote = 0;

    nwrote = write(servFD, &ackF, sizeof(FRAME));
    printFrame("Server send ACK:", &ackF);
    if (nwrote == -1){
        printf("server ack send error: fd %d giving error %s.\n", servFD, strerror(errno));
        return -1;
    }
    else {
        printf("server ack sent: %d bytes to %d.\n", nwrote, servFD);
        return servFD;
    }
}

/**
 * 
 * initFrame
 * Create, initialize, and return a new empty frame.
 * 
*/
FRAME initFrame(){
    FRAME newFrame;
    memset(&newFrame, 0, sizeof(FRAME));
    return newFrame;
}