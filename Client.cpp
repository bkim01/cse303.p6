#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
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
#include "Client.h"
#include <iostream>
#include <fstream>

using namespace std;

char* stop = "$";
int encryptFile;

void help(char *progname)
{
	printf("Usage: %s [OPTIONS]\n", progname);
	printf("Perform a PUT or a GET from a network file server\n");
	printf("  -P    PUT file indicated by parameter\n");
	printf("  -G    GET file indicated by parameter\n");
	printf("  -s    server info (IP or hostname)\n");
	printf("  -p    port on which to contact server\n");
	printf("  -S    for GETs, name to use when saving file locally\n");
	printf("  -C    use checksums for PUT and GET\n");
	printf("  -e    use encryption, with public.pem and private.pem\n");
}

void die(const char *msg1, const char *msg2)
{
	fprintf(stderr, "%s, %s\n", msg1, msg2);
	exit(0);
}

/*
* connect_to_server() - open a connection to the server specified by the
*                       parameters
*/
int connect_to_server(char *server, int port)
{
	int clientfd;
	struct hostent *hp;
	struct sockaddr_in serveraddr;
	char errbuf[256];                                   /* for errors */

	/* create a socket */
	if((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		die("Error creating socket: ", strerror(errno));
	}

	/* Fill in the server's IP address and port */
	if((hp = gethostbyname(server)) == NULL)
	{
		sprintf(errbuf, "%d", h_errno);
		die("DNS error: DNS error ", errbuf);
	}
	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	bcopy((char *)hp->h_addr_list[0], (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
	serveraddr.sin_port = htons(port);

	/* connect */
	if(connect(clientfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
	{
		die("Error connecting: ", strerror(errno));
	}
	return clientfd;
}

void response(int fd, ssize_t nsofar, size_t nremain, char* bufp) {
	while(1) {
		if((nsofar = read(fd, bufp, nremain)) < 0) {
			if(errno != EINTR) {
				die("Reading error: ", strerror(errno));
			}
			continue;
		}
		if(nsofar == 0) {
			die("Server error: ", "received EOF");
		}
		bufp += nsofar;
		nremain -= nsofar;
		//Stop reading once you've hit the stop character
		if(!strcmp(bufp, stop)) {
			break;
		}
	}
	bzero((bufp - strlen(stop)), strlen(stop));
}

/*
* echo_client() - this is dummy code to show how to read and write on a
*                 socket when there can be short counts.  The code
*                 implements an "echo" client.
*/
// void echo_client(int fd)
// {
// 	// main loop
// 	while(1)
// 	{
// 		/* set up a buffer, clear it, and read keyboard input */
// 		const int MAXLINE = 8192;
// 		char buf[MAXLINE];
// 		bzero(buf, MAXLINE);
// 		if(fgets(buf, MAXLINE, stdin) == NULL)
// 		{
// 			if(ferror(stdin))
// 			{
// 				die("fgets error", strerror(errno));
// 			}
// 			break;
// 		}
//
// 		/* send keystrokes to the server, handling short counts */
// 		size_t n = strlen(buf);
// 		size_t nremain = n;
// 		ssize_t nsofar;
// 		char *bufp = buf;
// 		while(nremain > 0)
// 		{
// 			if((nsofar = write(fd, bufp, nremain)) <= 0)
// 			{
// 				if(errno != EINTR)
// 				{
// 					fprintf(stderr, "Write error: %s\n", strerror(errno));
// 					exit(0);
// 				}
// 				nsofar = 0;
// 			}
// 			nremain -= nsofar;
// 			bufp += nsofar;
// 		}
//
// 		/* read input back from socket (again, handle short counts)*/
// 		bzero(buf, MAXLINE);
// 		bufp = buf;
// 		nremain = MAXLINE;
// 		while(1)
// 		{
// 			if((nsofar = read(fd, bufp, nremain)) < 0)
// 			{
// 				if(errno != EINTR)
// 				{
// 					die("read error: ", strerror(errno));
// 				}
// 				continue;
// 			}
// 			/* in echo, server should never EOF */
// 			if(nsofar == 0)
// 			{
// 				die("Server error: ", "received EOF");
// 			}
// 			bufp += nsofar;
// 			nremain -= nsofar;
// 			if(*(bufp-1) == '\n')
// 			{
// 				*bufp = 0;
// 				break;
// 			}
// 		}
//
// 		/* output the result */
// 		printf("%s", buf);
// 	}
// }
RSA * createRSA(unsigned char * key, int pub)
{
	RSA *rsa= NULL;
	BIO *keybio ;
	keybio = BIO_new_mem_buf(key, -1);
	if (keybio==NULL)
	{
		printf( "Failed to create key BIO");
		return 0;
	}
	if(pub)
	{
		rsa = PEM_read_bio_RSA_PUBKEY(keybio, &rsa,NULL, NULL);
	}
	else
	{
		rsa = PEM_read_bio_RSAPrivateKey(keybio, &rsa,NULL, NULL);
	}

	return rsa;
}

int RSAEncrypt(char* contents, int size, char* encrypted, int getOrPut) {
	//The public key buffer
	//put = encrypt then send

	int e_size;
	//RSA* rsa = RSA_new();
	if(getOrPut == 0){
		printf("beginning of RS encrpy\n");
		printf("opening public.pem\n");

		FILE* pub_pem = fopen("public.pem", "r");
		printf("size is %d\n", size);
		unsigned char * buffer = (unsigned char *)malloc(2048 * sizeof(unsigned char));
		int bytes_read = fread(buffer, sizeof(unsigned char), 2048, pub_pem);
		printf("buffer is %s\n", buffer);
		printf("before createRSA \n");
		RSA * rsa = createRSA(buffer, 1);
		//printf("loading encrpyion keys\n");
		//printf("puby is:'%s'",puby);
		//rsa = PEM_read_RSA_PUBKEY(pub_pem, &rsa, NULL, NULL);
		printf("after createRSA\n");
		e_size = RSA_public_encrypt(size, (unsigned char*)contents, (unsigned char *)encrypted, rsa, RSA_PKCS1_PADDING);
		printf("content:%s\nencrypted%s\n", contents, encrypted);
		//printf("after encryption load\n");
		//BIO_free(b);
		//Close the file from which we got the
		fclose(pub_pem);
		//The size of the encrypted data
		//int max_encrypt = RSA_size(rsa_public);

		//The size of the encryption and where to store it
		//e_size = RSA_public_encrypt(size, (const unsigned char*)contents, (unsigned char *)encrypted, rsa, RSA_PKCS1_PADDING);
		printf("The encrypted data in the method is %s\n", encrypted);
		printf("The size is %i\n", e_size);
	}
	else{
		printf("beginning of RS decrypt\n");
		FILE* priv_pem = fopen("private.pem", "r");
		unsigned char * buffer = (unsigned char *)malloc(2048 * sizeof(unsigned char));
		int bytes_read = fread(buffer, sizeof(unsigned char), 2048, priv_pem);
		printf("decrypt buffer is %s\n", buffer);
		printf("decrypt before createRSA \n");
		RSA * rsa = createRSA(buffer, 0);
		e_size = RSA_private_decrypt(size, (unsigned char*)contents, (unsigned char *)encrypted, rsa, RSA_PKCS1_PADDING);
		//rsa = PEM_read_RSAPrivateKey(priv_pem, &rsa, NULL, NULL);

		printf("size:%d\ncontents:%s\n", size, contents);
		//e_size = RSA_private_decrypt(size, (const unsigned char*)contents, (unsigned char *)encrypted, rsa, RSA_PKCS1_PADDING);
		printf("The decrypting data in the method is %s\n", encrypted);
		printf("The esize is %i\n", e_size);
		fclose(priv_pem);


	}
	//The buffer for the decrypted data
	// unsigned char* decrypt_buf = (unsigned char*) malloc(1000);
	//
	// //Create a buffer to hold the private kemake
	//
	// //Get the private key from the file
	// FILE* priv_pem = fopen("private.pem", "r");
	// i = 0;
	// //Add each character of the private key to the buffer
	// char * priv = (char *)malloc(size * sizeof(char));
	// //bytes_read = fread(priv, sizeof(char), size, priv_pem);
	//
	// fclose(priv_pem);
	// //Decrypt the data using the private key
	//b = BIO_new_mem_buf((void*) priv, (int)strlen(priv));
	//RSA* rsa_private = PEM_read_bio_RSAPrivateKey(b, NULL, NULL, NULL);
	//RSA_private_decrypt(e_size, (const unsigned char *)encrypted, (unsigned char *)decrypt_buf, rsa_private, RSA_PKCS1_PADDING);
	//printf("The decrypted file says  %s\n", decrypt_buf);
	//Return the encrypted data in bytes
	return e_size;
}
/*
* put_file() - send a file to the server accessible via the given socket fd
*/
void put_file(int fd, char *put_name, int checkSum)
{
	/* TODO: implement a proper solution, instead of calling the echo() client */
	//echo_client(fd);
	char strArr[8192];
	off_t size;
	struct stat st;
	//Check that the put_name file is in the directory
	if(stat(put_name, &st) == 0) {
		size = st.st_size;
	}
	//Else print an error
	else {
		printf("The PUT file could not be opened");
		exit(0);
	}
	//A buffer for the encrypted data
	unsigned char encrypted[8000];
	//The size of the message's buffer is allocated
	//The size of the encrypted data (if encryption is chosen)
	int e_size = 0;
	//The put file pointer
	FILE* putf;
	int i = 0;
	putf = fopen(put_name, "r");
	//If the PUT file cannot be opened, print an error and exit
	if(!putf) {
		perror("The PUT file could not be opened\n");
		exit(0);
	}
	char c;
	//Keep reading from the file and add to the buffer of the message
	char * buffer = (char *)malloc(size * sizeof(char));
	int bytes_read = fread(buffer, sizeof(char), size, putf);


	fclose(putf);
	printf("client put buffer is :'%s'\n", buffer);
	if(checkSum == 1) {
		//unsigned char sum[256];
		//bzero(sum, 256);
		unsigned char * sum = (unsigned char *)malloc(sizeof(char) * size);
		//Print before and after calculating the checksum
		printf("The file buffer before the checksum is '%s'\n", buffer);
		printf("in checksum bytes is : %d\n", size);

		//cout << "md5 of abc" << MD5("abc") << endl

		MD5((unsigned char *)buffer, size, sum);
		//printf("The checksummed file buffer is %s\nThe checksum is %02x\n", buffer, sum);
		char mdString[33];

		for(int i = 0; i < 16; i++){
			sprintf(&mdString[i*2], "%02x", (unsigned int)sum[i]);
		}

		printf("md5 digest: %s\n", mdString);
		sprintf(strArr, "%s\n<%s>\n<%lu bytes>\n<%s>\n<%s>%s", "PUTC", put_name, size ,mdString, buffer, stop);
	}
	//Otherise no checksum is wanted here
	else {
		//If the encryption check is set to 1, print the encryption
		if(encryptFile == 1) {
			//printf("not here\n");
			e_size = RSAEncrypt(buffer, size, (char *)encrypted, 0);
			//printf("here\n");
			sprintf(strArr, "%s\n<%s>\n<%lu bytes>\n<%s>%s", "PUT", put_name, e_size, encrypted, stop);
		}
		else {
			sprintf(strArr, "%s\n<%s>\n<%lu bytes>\n<%s>%s", "PUT", put_name, size, buffer, stop);
		}
	}

	printf("Client sends:\n%s\n", strArr);
	//sprintf(strArr, "%s\n%s\n%ul\n%s\n$", "PUT", put_name, size, tempArr);
	size_t n = strlen(strArr);
	size_t nremain = n;
	ssize_t nsofar;
	char* bufp = strArr;
	while(nremain > 0) {
		if((nsofar = write(fd, bufp, nremain)) <= 0) {
			if(errno != EINTR) {
				fprintf(stderr, "Write error: %s\n", strerror(errno));
				exit(0);
			}
			nsofar = 0;
		}
		nremain -= nsofar;
		bufp += nsofar;
	}

	const int MAXLINE = 8192;
	char bufnew[MAXLINE];   /* a place to store text from the client */
	bzero(bufnew, MAXLINE);
	/* read from socket, recognizing that we may get short counts */
	char *bufpnew = bufnew;              /* current pointer into buffer */
	ssize_t nremainnew = MAXLINE;     /* max characters we can still read */
	size_t nsofarnew;
	while (1) {
		/* read some data; swallow EINTRs */
		if ((nsofar = read(fd, bufpnew, nremainnew)) < 0) {
			if (errno != EINTR) {
				die("read error: ", strerror(errno));
			}
			printf("recieved and EINTR\n");
			continue;
		}
		//If nothing is read, simply return
		if(nsofarnew == 0) {
			break;
		}
		bufpnew += nsofarnew;
		nremainnew -= nsofarnew;
		//Stop once you've hit the stop character
		if(!strcmp(bufpnew, stop)) {
			break;
		}
	}

	bzero((bufpnew - strlen(stop)), strlen(stop));
	//Print the buffer at the end
	printf("bufnew: \n%s\n", bufnew);
	return;
}

/*
* get_file() - get a file from the server accessible via the given socket
*              fd, and save it according to the save_name
*/
void get_file(int fd, char *get_name, char *save_name, int checkSum)
{
	char strArr[8192];
	if(checkSum == 1){
		sprintf(strArr, "%s <%s>\n%s", "GETC", get_name, stop);
		printf("client sends get request:\n%s\n", strArr);
	}
	else{
		sprintf(strArr, "%s <%s>\n%s", "GET", get_name, stop);
		printf("client sends get request:\n%s\n", strArr);
	}
	size_t n = strlen(strArr);
	size_t nremainnew = n;
	ssize_t nsofarnew;
	char* arrp = strArr;
	while(nremainnew > 0) {
		if((nsofarnew = write(fd, arrp, nremainnew)) <= 0) {
			if(errno != EINTR) {
				fprintf(stderr, "Write error: %s\n", strerror(errno));
				exit(0);
			}
			nsofarnew = 0;
		}
		nremainnew -= nsofarnew;
		arrp += nsofarnew;
	}

	const int MAXLINE = 8192;
	char      buf[MAXLINE];   /* a place to store text from the client */
	bzero(buf, MAXLINE);
	/* read from socket, recognizing that we may get short counts */
	char *bufp = buf;              /* current pointer into buffer */
	ssize_t nremain = MAXLINE;     /* max characters we can still read */
	size_t nsofar;
	while (1) {
		/* read some data; swallow EINTRs */
		if ((nsofar = read(fd, bufp, nremain)) < 0) {

			if (errno != EINTR) {
				die("read error: ", strerror(errno));
			}
			printf("recieved and EINTR\n");
			continue;
		}

		printf("nosofar: \n%d\n", nsofar);

		if (nsofar == 0) {
			break;
		}
		/* update pointer for next bit of reading */
		bufp += nsofar;
		nremain -= nsofar;
		//Stop reading once you've hit the stop character
		if(!strcmp(bufp, stop)) {
			break;
		}
	}
	//Print the current buffer of the file
	printf("current buffer '%s'\n", buf);
	bzero((bufp - strlen(stop)), strlen(stop));
	//printf("contents of bufp: %s\ncontents of buf: %s", bufp, buf);
	char command[15][MAXLINE];
	bzero(command, MAXLINE);
	int i = 0;
	int buffer_size = 0;
	//Incrementers for the iteratoring through the contents
	int h_i = 0;
	int v_i = 0;

	char bcarrot = '<';
	char ecarrot = '>';

	int j = 0;
	int begin = 0;
	int end = 0;
	char * token;
	int okCheck = 1;

	bufp = buf;
	if(!strncmp(buf, "OK", 2)){
		strcpy(command[0], "OK");
	}
	else{
		okCheck = 0;
		printf("error message from server");
	}

	token = strchr(buf, bcarrot);

	char * filename = (char *)malloc(8192 * sizeof(char));
	char * byteSize = (char *)malloc(8192 * sizeof(char));
	char * cSum = (char *)malloc(8192 * sizeof(char));
	char * fcontent = (char *)malloc(8192 * sizeof(char));

	if(checkSum == 0){
		while(j < 3 && okCheck){
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
	else{
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

	printf("filename is: '%s'\n", filename);
	printf("byteSize is: '%s'\n", byteSize);
	printf("cSum is: '%s'\n", cSum);
	printf("file contents is: '%s'\n", fcontent);


	int done = 0;
	//See if OK is printed and flag it as a successful run
	int k = 0;
	// for(k = 0; k < sizeof(command); k++){
	// 	printf("command[%d] '%s'\n", k, command[k]);
	// }


	if(buf[0] == 'O') {
		done = 1;
	}
	else {
		printf("Could not read any responses from the server\n");
	}

	int sizeCount;
	sscanf(byteSize, "%d", &sizeCount);
	//A decrypted file buffer
	unsigned char decryptFile[8000];
	//bzero(decryptFile, 8000);
	//The size of the decrypted file buffer
	int d_size = 0;
	int e_size;
	//If the file is encrypted, then we need to decrypt it
	unsigned char decrypted[8000];
	if(encryptFile == 1) {
		//printf("not here\n");
		printf("buffer :%s\nsize:%d\n", fcontent, sizeCount);
		e_size = RSAEncrypt(fcontent, sizeCount, (char *)decrypted, 1);

	}
	if(done) {
		//Create a file to use for GET
		int validCSum = 0;
		if(checkSum){
			int size;
			sscanf(byteSize, "%d", &size);
			//printf("size is %d\n", size);
			unsigned char * sum = (unsigned char *)malloc(sizeof(char) * size);
			//Print before and after calculating the checksum
			printf("fcontent is '%s' size is '%d'\n", fcontent, size);
			MD5((unsigned char *)fcontent, size, sum);
			//printf("The checksummed file buffer is %s\nThe checksum is %02x\n", buffer, sum);
			char mdString[33];

			for(int i = 0; i < 16; i++){
				sprintf(&mdString[i*2], "%02x", (unsigned int)sum[i]);
			}
			printf("mdString is : %s\n", mdString);
			if(!strcmp(mdString, cSum)){
				validCSum = 1;
			}
			else{
				printf("checksum was invalid.\n");
			}
		}

		if(validCSum || !checkSum){
			FILE* getf;
			printf("save_name is:\n%s\n", save_name);
			if((getf = fopen(save_name, "w")) != NULL) {
				int j = 0;
				//If the file is encrypted, decrypt it and show the contents
				if(encryptFile) {
					printf("ofstream in encryptfile if statement\n");
					std::ofstream myfile;
					myfile.open(save_name);
					myfile << decrypted;
					myfile.close();
				}
				else {
					//Print out the file contents
					printf("i am doing that ofstream stuff\n");
					std::ofstream myfile;
					myfile.open(save_name);
					myfile << fcontent;
					myfile.close();
				}
			}
			//If the file does not exist/cannot be opened, print an error
			else {
				printf("The file could not be opened\n");
			}
		}
	}
}

/*
* main() - parse command line, open a socket, transfer a file
*/
int main(int argc, char **argv)
{
	/* for getopt */
	long  opt;
	char *server = NULL;
	char *put_name = NULL;
	char *get_name = NULL;
	int   port;
	char *save_name = NULL;

	check_team(argv[0]);
	int checkSum = 0;
	encryptFile = 0;
	/* parse the command-line options. */
	while((opt = getopt(argc, argv, "hs:P:G:S:p:Ce")) != -1)
	{
		switch(opt)
		{
			case 'h': help(argv[0]); break;
			case 's': server = optarg; break;
			case 'P': put_name = optarg; break;
			case 'G': get_name = optarg; break;
			case 'S': save_name = optarg; break;
			case 'p': port = atoi(optarg); break;
			case 'C': checkSum = 1; break;
			case 'e': encryptFile = 1; break;

		}
	}

	/* open a connection to the server */
	int fd = connect_to_server(server, port);

	/* put or get, as appropriate */
	if(put_name)
	{
		put_file(fd, put_name, checkSum);
	}
	else
	{
		get_file(fd, get_name, save_name, checkSum);
	}

	/* close the socket */
	int rc;
	if((rc = close(fd)) < 0)
	{
		die("Close error: ", strerror(errno));
	}
	exit(0);
}
