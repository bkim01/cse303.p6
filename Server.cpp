#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/md5.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "support.h"
#include "Server.h"
#include <fstream>
#include <iostream>

using namespace std;

int LRUCacheSize = 2;

void help(char *progname)
{
	printf("Usage: %s [OPTIONS]\n", progname);
	printf("Initiate a network file server\n");
	printf("  -m    enable multithreading mode\n");
	printf("  -l    number of entries in the LRU cache\n");
	printf("  -p    port on which to listen for connections\n");
}

void die(const char *msg1, char *msg2)
{
	fprintf(stderr, "%s, %s\n", msg1, msg2);
	exit(0);
}

/*
* open_server_socket() - Open a listening socket and return its file
*                        descriptor, or terminate the program
*/
int open_server_socket(int port)
{
	int                listenfd;    /* the server's listening file descriptor */
	struct sockaddr_in addrs;       /* describes which clients we'll accept */
	int                optval = 1;  /* for configuring the socket */

	/* Create a socket descriptor */
	if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		die("Error creating socket: ", strerror(errno));
	}

	/* Eliminates "Address already in use" error from bind. */
	if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int)) < 0)
	{
		die("Error configuring socket: ", strerror(errno));
	}

	/* Listenfd will be an endpoint for all requests to the port from any IP
	address */
	bzero((char *) &addrs, sizeof(addrs));
	addrs.sin_family = AF_INET;
	addrs.sin_addr.s_addr = htonl(INADDR_ANY);
	addrs.sin_port = htons((unsigned short)port);
	if(bind(listenfd, (struct sockaddr *)&addrs, sizeof(addrs)) < 0)
	{
		die("Error in bind(): ", strerror(errno));
	}

	/* Make it a listening socket ready to accept connection requests */
	if(listen(listenfd, 1024) < 0)  // backlog of 1024
	{
		die("Error in listen(): ", strerror(errno));
	}

	return listenfd;
}

/*
* handle_requests() - given a listening file descriptor, continually wait
*                     for a request to come in, and when it arrives, pass it
*                     to service_function.  Note that this is not a
*                     multi-threaded server.
*/
void handle_requests(int listenfd, void (*service_function)(int, int), int param, bool multithread)
{
	while(1)
	{
		/* block until we get a connection */
		struct sockaddr_in clientaddr;
		memset(&clientaddr, 0, sizeof(sockaddr_in));
		socklen_t clientlen = sizeof(clientaddr);
		int connfd;
		if((connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen)) < 0)
		{
			die("Error in accept(): ", strerror(errno));
		}

		/* print some info about the connection */
		struct hostent *hp;
		hp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, sizeof(clientaddr.sin_addr.s_addr), AF_INET);
		if(hp == NULL)
		{
			fprintf(stderr, "DNS error in gethostbyaddr() %d\n", h_errno);
			exit(0);
		}
		char *haddrp = inet_ntoa(clientaddr.sin_addr);
		printf("server connected to %s (%s)\n", hp->h_name, haddrp);

		/* serve requests */
		service_function(connfd, param);

		/* clean up, await new connection */
		if(close(connfd) < 0)
		{
			die("Error in close(): ", strerror(errno));
		}
	}
}

struct getput {
	//The name of the file to put or get
	char* filename;
	//The type of command (PUT or GET)
	int type;
	//The size of the file in bytes
	int bytes;
	//Boolean to check whether a checkSum should be computed
	int checkSum;
};

//Structure to create linked list of the LRU cache
struct lru_cache {
	//The name of the file in the current position of the LRU
	char* name;
	//The size of the LRU thus far
	int lru_size;
	//The contents of the LRU at a certain position
	char* contents;
	//The frequency count (newness) of a file in the cache
	int freq;
};

//Create a global LRU cache
struct lru_cache** lru;
//Give it a size and a max size (max size will be chosen by user)
int size_cache;
int max_cache;

//Initialize the cache and allocate memory for it
void initialize_lru(int size) {
	max_cache = size;
	lru = (struct lru_cache**)malloc(sizeof(struct lru_cache) * size);
	size_cache = 0;
}

/*
* file_server() - Read a request from a socket, satisfy the request, and
*                 then close the connection.
*/
//Character to stop the transmission
char st[1] = {'$'};
char * stop = st;

//Get the name of the file that is requested from the client
int getName(char* fil, struct getput* req) {
	//Check to see if the file exists
	int exists = 0;
	//The length of the filename
	int length = strlen(fil);
	if(!strlen(fil)) {
		return 0;
	}
	fil = fil + 1;
	fil[length - 2] = '\0';

	if(fil[0] == ' '){
		fil = fil + 1;
	}

	//The file exists if it can be found
	if(fil[length - 1] == '\n') {
		fil[length - 1] = '\0';
		exists = 1;
	}
	req->filename = fil;

	//If it exists, type
	if(exists == 1) {
		fil[length - 1] = '\n';
	}
	//printf("fil when we send carrot %s\n", fil);

	//Return 1 if the file is found by the server
	return 1;
}

//Get the type of command that is requested (PUT or GET)
int getType(char* command, struct getput* req) {
	//Check to see whether the command given is a PUT or a GET
	//printf("inside getType command is %s\n", command);

	//Check whether the command given is a PUTC or a GETC (a PUT/GET with a
	//checksum
	if(!strncmp(command, "GETC", 4)) {
		req->type = 1;
		req->checkSum = 1;
		return 1;
	}
	if(!strcmp(command, "PUTC")) {
		req->type = 2;
		req->checkSum = 1;
		return 1;
	}
	if(!strncmp(command, "GET", 3)) {
		req->type = 1;
		req->checkSum = 0;
		return 1;
	}
	if(!strcmp(command, "PUT")) {
		req->type = 2;
		req->checkSum = 0;
		return 1;
	}
	//If some other command is given, return 0 (not possible command)
	return 0;
}

//Get the size of the file of the GET/PUT request given
int getSize(char* size, struct getput* req) {
	int intSize;
	if(!sscanf(size, "<%d>", &intSize)) {
		return 0;
	}
	else if(intSize < 1) {
		return 0;
	}
	req->bytes = intSize;
	return 1;
}

//Fetch the file from the LRU cache
struct lru_cache* getCache(char* cachedFile) {
	int i;
	//Go through the cache and look for the file
	for(i = 0; i < size_cache; i++) {
		//Check to see that the spot in the LRU exists and whether there is a match
		if(lru[i] != NULL && !strcmp(lru[i]->name, cachedFile)) {
			printf("%s was in the cache at spot %i\n", cachedFile, i);
			//Return the file
			return lru[i];
		}
	}
	//If there was nothing found, return NULL
	return NULL;
}

//Updates the frequency of the files in the LRU so that they are not removed
void updateLRUFreq(struct lru_cache* cache) {
	int i = 0;
	//Update the frequency of the file in the LRU so that it is not
	//the next one removed from the cache
	while(i < size_cache && lru[i] != NULL) {
		lru[i]->freq += 1;
		i++;
	}
	cache->freq = 0;
}

//Remove the first file (the one there the longest) from the cache queue
int removeFirst() {
	struct lru_cache* firstFile = NULL;
	int first = 0;
	int i = 0;
	int firstI = 0;
	//Go through the
	while(i < size_cache) {
		//If there is nothing at that spot, ret
		if(lru[i] == NULL) {
			return i;
		}
		//Go by the frequencies and find the first file so you can remove it
		else {
			//Keep going back to find the first file in the cache with the
			//lowest frequency
			if(lru[i]->freq > first) {
				first = lru[i]->freq;
				firstFile = lru[i];
				firstI = i;
			}
		}
		i++;
	}
	//If the first file is not NULL, free its memory
	if(firstFile != NULL) {
		free(firstFile);
	}
	//Print that the first file was removed
	printf("File at position %i is the first\n", i);
	//Return the freed memory space
	return firstI;
}

//Add a file to the LRU cache
void addCache(struct lru_cache* cachedFile) {
	//Try to fetch the file from the cache
	struct lru_cache* oldFile = getCache(cachedFile->name);
	//If the file can be fetched from the cache, replace the old copy with
	//the new one
	if(oldFile != NULL) {
		oldFile = cachedFile;
	}
	//Add the new file if you still have cache space
	else if(size_cache < max_cache) {
		lru[size_cache] = cachedFile;
		//Increment the size counter
		size_cache++;
		//printf("%s was added to the cache at spot %i\n", lru[size_cache - 1]->name, size_cache);
	}
	//If no more space, remove the first file in the cache
	else {
		int remove = removeFirst();
		lru[remove] = (struct lru_cache*)malloc(sizeof(struct lru_cache));
		lru[remove] = (struct lru_cache*)cachedFile;
		printf("Replaced oldest file in position %i\n", remove);
	}
	//Update the frequencies so you can evict old files from LRU
	updateLRUFreq(cachedFile);
}

//Read the response from the client
void clientResponse(int connfd, char* result) {
	int nremain = strlen(result);
	//Create a buffer to hold the response
	char arr[8000];
	int nsofar = 0;
	char* bufp = arr;
	bzero(arr, 8000);
	//Print the resuls and the terminating character in the array buffer
	sprintf(arr, "%s%s", result, stop);
	while(nremain > 0) {
		//printf("printing result from clientResponse:\n%s\n", arr);
		if((nsofar = write(connfd, result, nremain)) <= 0) {
			if(errno != EINTR) {
				die("Write error: ", strerror(errno));
			}
			nsofar = 0;
		}
		nremain -= nsofar;
		bufp += nsofar;
	}
}

//Create a file based on the contents that are being read in
int create(int connfd, struct getput* req, char* contents, int nsofar) {
	int file_reading = req->bytes;
	if(contents[0] == '<'){
		//printf("here");
		contents = contents + 1;
	}
	if(0 && nsofar < file_reading) {
		char* total_buf = contents + nsofar;
		int current_read = 0;
		while(1) {
			if((current_read = read(connfd, total_buf, (file_reading - nsofar)) < 0)) {
				if(errno != EINTR) {
					die("read error: ", strerror(errno));
				}
				printf("received and EINTR\n");
				continue;
			}
			//If there is nothing being read, print an error message
			if(current_read == 0) {
				fprintf(stderr, "Received no contents for a file\n");
			}
			nsofar += current_read;
			total_buf += nsofar;
			if(file_reading <= nsofar) {
				break;
			}
		}
	}
	// contents = contents + 1;
	// contents[strlen(contents) - 2] = '\0';
	//printf("Contents of file: \n%s\n", contents);
	FILE* putf = NULL;
	//Open the put file
	putf = fopen(req->filename, "w");
	//If the putf can be opened, store the file into the LRU
	if(putf != NULL) {
		//Print the contents and close the file
		int werror = fputs(contents, putf);
		fclose(putf);
		//Make an entry in the LRU cache
		struct lru_cache* putArr = (struct lru_cache *)malloc(sizeof(struct lru_cache));
		putArr->name = strdup(req->filename);
		putArr->contents = strdup(contents);
		putArr->lru_size = req->bytes;
		addCache(putArr);
		//Return true, that it was fetched from the LRU
		return 1;
	}
	else {
		printf("The PUT file could not be opened");
	}
	//Return true, that the file was created
	return 1;
}

//Calculate the checksum for a certain buffer if the flag is raised
int checkSum(char* checksum, struct getput* req, char* contents, char * ret) {
	//Hold the checksum buffer
	unsigned char * sum = (unsigned char *)malloc(sizeof(char) * req->bytes);
	//Calculate the sum using this buffer

	//printf("client given checksum is %s\n", checksum);
	//printf("The file buffer before the checksum is \n'%s'", contents);

	//Run the MD5 algorithm on the checksum
	//printf("fcontent is '%s' size is '%d'\n", contents, req->bytes);

	//printf("in checksum bytes is : %d\n", req->bytes);
	MD5((unsigned char *)contents, req->bytes, sum);
	//sprintf(arr, "%x", sum);
	//MD5((unsigned char *)buffer, size, sum);
	//printf("The checksummed file buffer is %s\nThe checksum is %02x\n", buffer, sum);
	char mdString[33];

	for(int i = 0; i < 16; i++)
	sprintf(&mdString[i*2], "%02x", (unsigned int)sum[i]);
	//printf("The contents are:\n'%s'\nServer checksum:\n %s\nClient checksum:\n %s\n", contents, arr, checksum);
	//Compare the two and see if they are the same and print the sum
	//printf("mdString is : '%s' checksum is '%s'", mdString, checksum);
	if(checksum == NULL){
		strcpy(ret, mdString);
		return 1;
	}

	if(!strcmp(mdString, checksum)) {
		//printf("The checksum is %s", mdString);
		return 1;
	}
	//If the check sum does not agree
	printf("The check sum is %s and it does not agree\n", mdString);
	//Return false if they did not match after printin
	return 0;
}

/*
* file_server() - Read a request from a socket, satisfy the request, and
*                 then close the connection.
*/
void file_server(int connfd, int lru_size) {
	/* TODO: set up a few static variables here to manage the LRU cache of
	files */

	/* TODO: replace following sample code with code that satisfies the

	requirements of the assignment */

	//int getPutCounter = 0;
	//The current request (whether a GET or a PUT)
	struct getput* req = (struct getput *)malloc(sizeof(struct getput));
	//The contents of the file being opened
	char* fileContents;
	int found;
	while(1) {
		const int MAXLINE = 8192;
		char command[15][MAXLINE];
		int i = 0;
		char buf[MAXLINE];
		bzero(buf, MAXLINE);
		char* bufp = buf;
		ssize_t nremain = MAXLINE;
		size_t nsofar;
		while(1) {
			if((nsofar = read(connfd, bufp, nremain)) < 0) {
				if(errno != EINTR) {
					die("read error: ", strerror(errno));
				}
				printf("received an EINTR\n");
				continue;
			}

			if(nsofar == 0) {
				return;
			}

			bufp += nsofar;
			nremain -= nsofar;
			//If we get an end transmission, break
			if(!strcmp((bufp - strlen(stop)), stop)) {
				break;
			}
		}
		printf("%s", buf);
		bzero((bufp - strlen(stop)), strlen(stop));
		//Inrementers usedF for the commands to iterate through them
		int commandv_i = 0;
		int commandh_i = 0;
		int read_contents = 0;
		int read_size = 3;
		//Iterate through the number of bytes read so far
		while(i < nsofar) {
			//If you reach a new line and cannot read anymore, stop reading that line
			//and increment the indices
			if(buf[i] == '\n' && commandv_i < read_size) {
				command[commandv_i][commandh_i] = '\0';
				commandv_i++;
				i++;
				commandh_i = 0;
				//If we are currently at increment of 1, we check to see the type
				//of the command and act accordingly
				if(commandv_i == 1) {
					found = getType(command[0], req);
					if(req->checkSum == 1) {
						read_size++;
					}
					if(found) {
						*bufp = 0;
					}
					//Else continue through the loop
					else {
						continue;
					}
				}
				continue;
			}
			if(commandv_i == 3) {
				read_contents++;
			}
			command[commandv_i][commandh_i] = buf[i];
			commandh_i++;
			i++;
		}
		//If there are too little rows, print an error and continue through
		//the loop
		if(commandv_i < 1) {
			printf("Input does not have enough commands to work\n");
			continue;
		}
		//Check if we can get a type for the command
		found = getType(command[0], req);
		//If yes, prepare to read
		if(found == 1) {
			*bufp = 0;
		}
		//If no, continue through the loop
		else {
			continue;
		}
		//

		//printf("what is command[1] \n%s\n", command[0]);
		//printf("what is req->checkSum %d\n", req->checkSum);
		if(req->type == 1){
			if(req->checkSum == 1){
				//printf("here\n");
				char * getfilename = command[0];
				getfilename = getfilename + 5;
				found = getName(getfilename, req);
			}
			else{
				char * getfilename = command[0];
				getfilename = getfilename + 4;
				found = getName(getfilename, req);
			}
		}else{
			found = getName(command[1], req);
		}
		//If yes, prepare to read
		if(found == 1) {
			*bufp = 0;
		}
		//If it is a PUT request
		//printf("what is req->type %d\n", req->type);
		if(req->type == 2) {
			char bcarrot = '<';
			char ecarrot = '>';

			int j = 0;
			int begin = 0;
			int end = 0;
			char * token;
			int okCheck = 1;
			int putcCheck = 0;

			bufp = buf;
			//printf("buffer here :\n%s\n", bufp);

			token = strchr(buf, bcarrot);

			char * filename = (char *)malloc(8192 * sizeof(char));
			char * byteSize = (char *)malloc(8192 * sizeof(char));
			char * fcontent = (char *)malloc(8192 * sizeof(char));
			char * cSum = (char *)malloc(8192 * sizeof(char));


			if(req->checkSum == 1){
				while(j < 4){
					begin = token - buf + 1;
					token = strchr(token + 1, ecarrot);
					end = token - buf + 1;
					token = strchr(token + 1, bcarrot);

					if(j == 0){
						strncpy(filename, bufp + begin, end - begin - 1);

					}
					else if( j == 1){
						strncpy(byteSize, bufp + begin, end - begin - 1);
					}
					else if( j == 2){
						strncpy(cSum, bufp + begin, end - begin - 1);
					}
					else if( j == 3){
						strncpy(fcontent, bufp + begin, end - begin - 1);
					}
					j++;

				}
			}
			else{
				while(j < 3){
					begin = token - buf + 1;
					token = strchr(token + 1, ecarrot);
					end = token - buf + 1;
					token = strchr(token + 1, bcarrot);

					if(j == 0){
						strncpy(filename, bufp + begin, end - begin - 1);

					}
					else if( j == 1){
						strncpy(byteSize, bufp + begin, end - begin - 1);
					}
					else if( j == 2){
						strncpy(fcontent, bufp + begin, end - begin - 1);
					}
					j++;
				}
			}


			// printf("client filename is: '%s'\n", filename);
			// printf("client byteSize is: '%s'\n", byteSize);
			// printf("client file contents is: '%s'\n", fcontent);
			// printf("client checkSum is: '%s'\n", cSum);

			int shouldWrite = 0;
			if(req->checkSum == 1) {
				int size;
				sscanf(byteSize, "%d", &size);
				req->bytes = size;
				if(checkSum(cSum, req, fcontent, NULL)) {
					clientResponse(connfd, (char *)"OKC\n");
					shouldWrite = 1;
				}
			}
			else {
				clientResponse(connfd, (char *)"OK\n");
				shouldWrite = 1;
			}
			if(shouldWrite){
				std::ofstream myfile;
				myfile.open(filename);
				myfile << fcontent;
				myfile.close();
			}
			else{
				printf("error, put request failed");
			}
			continue;
		}
		else {
			//printf("inside get if statement\n");
			FILE* getf;
			char c = 'a';
			int i = 0;
			int size = 0;
			//printf("filename is '%s'\n", req->filename);
			getf = fopen(req->filename, "r");
			if(getf != NULL) {
				// while((c = fgetc(getf)) != EOF) {
				// 	size++;
				// }
				char * buffer = (char *)malloc(2048 * sizeof(char));
				int bytes_read = fread(buffer, sizeof(char), 2048, getf);
				//printf("in server get if statement, size is %d\n", bytes_read);
				req->bytes = bytes_read;
				//Close the file
				size = bytes_read;
				fclose(getf);
				fileContents = (char *)malloc(sizeof(char) * size);
				getf = fopen(req->filename, "r");
				//Get the file contents of the file and store them in the buffer
				while((c = fgetc(getf)) != EOF) {
					fileContents[i] = c;
					i++;
				}
				i = 0;
				fclose(getf);
				//Create a buffer to see if you can fetch the file from the cache
				struct lru_cache* getArr = getCache(req->filename);
				//If we could not fetch the file, make room for it and set the contents
				if(!getArr) {
					getArr = (struct lru_cache*)malloc(sizeof(struct lru_cache));
					getArr->name = strdup(req->filename);
					getArr->contents = strdup((const char *)buffer);
					getArr->lru_size = req->bytes;
					addCache(getArr);
				}
				//Store the size of the file and print the server response
				char sizeArr[8000];
				if(req->checkSum == 1){
					char * ret = (char *)malloc(req->bytes * sizeof(char));

					checkSum(NULL, req, fileContents, ret);
					//printf("ret is %s\n", ret);
					//printf("filecontents %s\n", fileContents);
					sprintf(sizeArr, "%s <%s>\n<%i bytes>\n<%s>\n<%s>\n", "OKC", getArr->name, getArr->lru_size, ret, getArr->contents);
					clientResponse(connfd, sizeArr);
				}
				else{
					sprintf(sizeArr, "%s <%s>\n<%i bytes>\n<%s>\n", "OK", getArr->name, getArr->lru_size, getArr->contents);
					clientResponse(connfd, sizeArr);
				}
			}
			else {
				printf("The file could not be opened");
			}
			break;
		}
	}
}
/*
* main() - parse command line, create a socket, handle requests
*/
int main(int argc, char **argv)
{
	/* for getopt */
	long opt;
	int  lru_size = 10;
	int  port     = 9000;
	bool multithread = false;

	check_team(argv[0]);

	/* parse the command-line options.  They are 'p' for port number,  */
	/* and 'l' for lru cache size, 'm' for multi-threaded.  'h' is also supported. */
	while((opt = getopt(argc, argv, "hml:p:")) != -1)
	{
		switch(opt)
		{
			case 'h': help(argv[0]); break;
			case 'l': lru_size = atoi(argv[0]); break;
			case 'm': multithread = true;	break;
			case 'p': port = atoi(optarg); break;
		}
	}

	initialize_lru(lru_size);
	/* open a socket, and start handling requests */
	int fd = open_server_socket(port);
	handle_requests(fd, file_server, lru_size, multithread);

	exit(0);
}
