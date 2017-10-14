// Amy Reed - yashd.c
// UTEID alr2434
// This HW was started based on the sockets client and server examples, daeomon examples, and thread examples from class.
// There is code directly copied from these examples.
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h> /* sockaddr_in */
#include <netinet/in.h> /* inet_addr() */
#include <arpa/inet.h>
#include <netdb.h> /* struct hostent */
#include <string.h> /* memset() */
#include <unistd.h> /* close() */
#include <stdlib.h> /* exit() */
#include <signal.h>

#define MAXHOSTNAME 80
#define BUFSIZE 8192

char buf[BUFSIZE];
char rbuf[BUFSIZE];
void GetUserInput();
void cleanup(char *buf);
void handleSignal(int signal);
char* concat(const char*, const char*);


int rc, cc;
int   sd;

int main(int argc, char **argv ) {
    signal(SIGTSTP, &handleSignal);
    signal(SIGINT, &handleSignal);
    int childpid;
    struct   sockaddr_in server;
    struct   sockaddr_in client;
    struct  hostent *hp, *gethostbyname();
    struct  servent *sp;
    struct sockaddr_in from;
    struct sockaddr_in addr;
    int fromlen;
    int length;
    char ThisHost[80];
    
    sp = getservbyname("echo", "tcp");
    
    /** get TCPClient Host information, NAME and INET ADDRESS */
    
    gethostname(ThisHost, MAXHOSTNAME);
    /* OR strcpy(ThisHost,"localhost"); */
    
    //printf("----TCP/Cleint running at host NAME: %s\n", ThisHost);
    if  ( (hp = gethostbyname(ThisHost)) == NULL ) {
	    fprintf(stderr, "Can't find host %s\n", argv[1]);
	    exit(-1);
    }
    bcopy ( hp->h_addr, &(server.sin_addr), hp->h_length);
    //printf("    (TCP/Cleint INET ADDRESS is: %s )\n", inet_ntoa(server.sin_addr));
    
    /** get TCPServer-ex2 Host information, NAME and INET ADDRESS */
    
    if  ( (hp = gethostbyname(argv[1])) == NULL ) {
	    addr.sin_addr.s_addr = inet_addr(argv[1]);
	    if ((hp = gethostbyaddr((char *) &addr.sin_addr.s_addr,
				sizeof(addr.sin_addr.s_addr),AF_INET)) == NULL) {
            fprintf(stderr, "Can't find host %s\n", argv[1]);
            exit(-1);
	    }
    }
    //printf("----TCP/Server running at host NAME: %s\n", hp->h_name);
    bcopy ( hp->h_addr, &(server.sin_addr), hp->h_length);
    //printf("    (TCP/Server INET ADDRESS is: %s )\n", inet_ntoa(server.sin_addr));
    
    /* Construct name of socket to send to. */
    server.sin_family = AF_INET; 
    /* OR server.sin_family = hp->h_addrtype; */
    server.sin_port = htons(3826);
    //server.sin_port = htons(atoi(argv[2]));
    /*OR    server.sin_port = sp->s_port; */
    
    /*   Create socket on which to send  and receive */
    
    sd = socket (AF_INET,SOCK_STREAM,0); 
    /* sd = socket (hp->h_addrtype,SOCK_STREAM,0); */
  
    if (sd<0) {
	    perror("opening stream socket");
	    exit(-1);
    }

    /** Connect to TCPServer-ex2 */
    if ( connect(sd, (struct sockaddr *) &server, sizeof(server)) < 0 ) {
        close(sd);
        perror("connecting stream socket");
        exit(0);
    }
    fromlen = sizeof(from);
    if (getpeername(sd,(struct sockaddr *)&from,&fromlen)<0){
	    perror("could't get peername\n");
        exit(1);
    }
    //printf("Connected to TCPServer1: ");
    //printf("%s:%d\n", inet_ntoa(from.sin_addr),
	//   ntohs(from.sin_port));
    if ((hp = gethostbyaddr((char *) &from.sin_addr.s_addr,
			    sizeof(from.sin_addr.s_addr),AF_INET)) == NULL)
	    fprintf(stderr, "Can't find host %s\n", inet_ntoa(from.sin_addr));
    else
	    //printf("(Name is : %s)\n", hp->h_name);
    childpid = fork();
    if (childpid == 0) {
        signal(SIGTSTP, SIG_IGN);
        signal(SIGINT, SIG_IGN);
	   GetUserInput();
    }
    
    /** get data from USER, send it SERVER,
      receive it from SERVER, display it back to USER  */
    
    for(;;) {
	    cleanup(rbuf);
	    if( (rc=recv(sd, rbuf, sizeof(buf), 0)) < 0){
	        perror("receiving stream  message");
	        exit(-1);
	    }
	    if (rc > 0){
	        rbuf[rc]='\0';
	        printf("%s", rbuf);
            fflush(stdout);
	} else {
	    printf("Disconnected..\n");
	    close (sd);
	    exit(0);
	}
  }
}

void cleanup(char *buf)
{
    int i;
    for(i=0; i<BUFSIZE; i++) buf[i]='\0';
}

void handleSignal(int signal) {
    const char* signal_name;
    sigset_t pending;

    switch(signal) {
        case SIGINT:
            signal_name = "SIGINT";
            char * buf3 = "CTL c\0";
            if (send(sd, buf3, strlen(buf3), 0) <0 )
                perror("sending stream message");
            break;
        case SIGTSTP:
            signal_name = "SIGTSTP";
            char* buf2 = "CTL z\0";
            if (send(sd, buf2, strlen(buf2), 0) <0 )
                perror("sending stream message");
            break;
        default:
            //printf("Can't find signal.\n");
            return;
    }
}

void GetUserInput()
{
    for(;;) {
        fflush(stdout);
    	//Check input for newline, and if so, reprint prompt.
        //fflush(stdout);
    	cleanup(buf);
    	rc=read(0,buf, sizeof(buf));
    	if (rc == 0) break;
        if(strcmp(buf,"\n") == 0) {
            printf(" #");
            
        } else {
            char* buf2 = concat("CMD ", buf);
            //printf("Sending %s", buf2);
        	if (send(sd, buf2, rc+4, 0) <0 )
        	    perror("sending stream message");
        }
    }
    printf ("EOF... exit\n");
    close(sd);
    kill(getppid(), 9);
    exit (0);
}

char* concat(const char *s1, const char *s2)
{
    char *result = malloc(strlen(s1)+strlen(s2)+1);//+1 for the null-terminator
    //in real code you would check for errors in malloc here
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}

