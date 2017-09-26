/*
NAME:        TCPServer 
SYNOPSIS:    TCPServer [port]

DESCRIPTION:  The program creates a TCP socket in the inet 
              listen for connections from TCPClients, 
              accept clients into private sockets, and 
              fork an echo process to ``serve'' the client.
              If [port] is not specified, the program uses any available port.

*/
#include <stdio.h>
/* socket(), bind(), recv, send */
#include <sys/types.h>
#include <sys/socket.h> /* sockaddr_in */
#include <netinet/in.h> /* inet_addr() */
#include <arpa/inet.h>
#include <netdb.h> /* struct hostent */
#include <string.h> /* memset() */
#include <unistd.h> /* close() */
#include <stdlib.h> /* exit() */
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/un.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>



#define MAXHOSTNAME 80
#define MAX_THREADS 50
#define true 1
#define false 0
#define COMPLETE true
#define RUNNING false

extern int errno;

#define PATHMAX 255
static char u_server_path[PATHMAX+1] = "/tmp";  /* default */
//static char u_socket_path[PATHMAX+1];
static char u_log_path[PATHMAX+1];
static char u_pid_path[PATHMAX+1];

typedef int bool;


struct threadInput {
    int psd;
    struct sockaddr_in from;
    bool threadComplete;
    int threadId;
};

/**
 * @brief  If we are waiting reading from a pipe and
 *  the interlocutor dies abruptly (say because
 *  of ^C or kill -9), then we receive a SIGPIPE
 *  signal. Here we handle that.
 */
void sig_pipe(int n) 
{
   perror("Broken pipe signal");
}


/**
 * @brief Handler for SIGCHLD signal 
 */
void sig_chld(int n)
{
  int status;

  fprintf(stderr, "Child terminated\n");
  wait(&status); /* So no zombies */
}

/**
 * @brief Initializes the current program as a daemon, by changing working 
 *  directory, umask, and eliminating control terminal,
 *  setting signal handlers, saving pid, making sure that only
 *  one daemon is running. Modified from R.Stevens.
 * @param[in] path is where the daemon eventually operates
 * @param[in] mask is the umask typically set to 0
 */
void daemon_init(const char * const path, uint mask)
{
  pid_t pid;
  char buff[256];
  static FILE *log; /* for the log */
  int fd;
  int k;

  /* put server in background (with init as parent) */
  if ( ( pid = fork() ) < 0 ) {
    perror("daemon_init: cannot fork");
    exit(0);
  } else if (pid > 0) /* The parent */
    exit(0);

  /* the child */

  /* Close all file descriptors that are open */
  for (k = getdtablesize()-1; k>0; k--)
      close(k);

  /* Redirecting stdin and stdout to /dev/null */
  if ( (fd = open("/dev/null", O_RDWR)) < 0) {
    perror("Open");
    exit(0);
  }
  dup2(fd, STDIN_FILENO);      /* detach stdin */
  dup2(fd, STDOUT_FILENO);     /* detach stdout */
  close (fd);
  /* From this point on printf and scanf have no effect */

  /* Redirecting stderr to u_log_path */
  log = fopen(u_log_path, "aw"); /* attach stderr to u_log_path */
  fd = fileno(log);  /* obtain file descriptor of the log */
  dup2(fd, STDERR_FILENO);
  close (fd);
  /* From this point on printing to stderr will go to /tmp/u-echod.log */

  /* Establish handlers for signals */
  if ( signal(SIGCHLD, sig_chld) < 0 ) {
    perror("Signal SIGCHLD");
    exit(1);
  }
  if ( signal(SIGPIPE, sig_pipe) < 0 ) {
    perror("Signal SIGPIPE");
    exit(1);
  }

  /* Change directory to specified directory */
  chdir(path); 

  /* Set umask to mask (usually 0) */
  umask(mask); 
  
  /* Detach controlling terminal by becoming sesion leader */
  setsid();

  /* Put self in a new process group */
  pid = getpid();
  setpgrp(); /* GPI: modified for linux */

  /* Make sure only one server is running */
  if ( ( k = open(u_pid_path, O_RDWR | O_CREAT, 0666) ) < 0 )
    exit(1);
  if ( lockf(k, F_TLOCK, 0) != 0)
    exit(0);

  /* Save server's pid without closing file (so lock remains)*/
  sprintf(buff, "%6d", pid);
  write(k, buff, strlen(buff));

  return;
}

void reusePort(int sock);
void* EchoServe(void*);

int main(int argc, char **argv ) {
    int   sd, psd;
    struct   sockaddr_in server;
    struct  hostent *hp, *gethostbyname();
    struct  servent *sp;
    struct sockaddr_in from;
    int fromlen;
    int length;
    char ThisHost[80];
    pid_t childpid;
    int pn;
    pthread_t p;
    struct threadInput threadInputList[MAX_THREADS];
    pthread_t threadList[MAX_THREADS];
    int currentThread = 0;
    int returnCode;

    if (argc > 1) {
        printf("Argc is greater than one\n");
        printf("U server path is: %s", u_server_path);
        strncpy(u_server_path, argv[1], PATHMAX); /* use argv[1] */
    }  
    strncat(u_server_path, "/", PATHMAX-strlen(u_server_path));
    strncat(u_server_path, argv[0], PATHMAX-strlen(u_server_path));
    strcpy(u_pid_path, u_server_path);
    strncat(u_pid_path, ".pid", PATHMAX-strlen(u_pid_path));
    strcpy(u_log_path, u_server_path);
    strncat(u_log_path, ".log", PATHMAX-strlen(u_log_path));

    daemon_init(u_server_path, 0); /* We stay in the u_server_path directory and file
                                    creation is not restricted. */

    
    sp = getservbyname("echo", "tcp");
    /* get TCPServer1 Host information, NAME and INET ADDRESS */

    gethostname(ThisHost, MAXHOSTNAME);
    /* OR strcpy(ThisHost,"localhost"); */
    
    printf("----TCP/Server running at host NAME: %s\n", ThisHost);
    if  ( (hp = gethostbyname(ThisHost)) == NULL ) {
        fprintf(stderr, "Can't find host %s\n", argv[1]);
        exit(-1);
    }
    bcopy ( hp->h_addr, &(server.sin_addr), hp->h_length);
    printf("    (TCP/Server INET ADDRESS is: %s )\n", inet_ntoa(server.sin_addr));

    
    
    /** Construct name of socket */
    server.sin_family = AF_INET;
    /* OR server.sin_family = hp->h_addrtype; */
    
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    if (argc == 1)
        server.sin_port = htons(0);  
    else  {
        pn = htons(atoi(argv[1])); 
        server.sin_port =  pn;
    }
    /*OR    server.sin_port = sp->s_port; */
    
    /** Create socket on which to send  and receive */
    
    sd = socket (AF_INET,SOCK_STREAM,IPPROTO_TCP); 
    /* OR sd = socket (hp->h_addrtype,SOCK_STREAM,0); */
    if (sd<0) {
	    perror("opening stream socket");
	    exit(-1);
    }
    /** this allow the server to re-start quickly instead of fully wait
	for TIME_WAIT which can be as large as 2 minutes */
    reusePort(sd);
    if ( bind( sd, (struct sockaddr *) &server, sizeof(server) ) < 0 ) {
	    close(sd);
	    perror("binding name to stream socket");
	    exit(-1);
    }
    
    /** get port information and  prints it out */
    length = sizeof(server);
    if ( getsockname (sd, (struct sockaddr *)&server,&length) ) {
	    perror("getting socket name");
	    exit(0);
    }
    printf("Server Port is: %d\n", ntohs(server.sin_port));
    fprintf(stderr, "The Server Port is: %d\n", ntohs(server.sin_port));
    
    /** accept TCP connections from clients and fork a process to serve each */
    listen(sd,4);
    fromlen = sizeof(from);


    for(;;){


        printf("****Waiting on new connection....\n");
	    psd  = accept(sd, (struct sockaddr *)&from, &fromlen);
        printf("****Got connection\n");
        struct threadInput argin = {psd, from, RUNNING,0};
        threadInputList[currentThread] = argin;
        printf("****Created thread\n");
        if ((returnCode = pthread_create(&threadList[currentThread], NULL, EchoServe, &threadInputList[currentThread]))) {
            fprintf(stderr, "****error: pthread_create, returnCode: %d\n", returnCode);
            return EXIT_FAILURE;
        }
        printf("****Thread created, incrementing current thread\n");
        currentThread++;

        /* block until all threads complete */
        for (int i = 0; i < currentThread; ++i) {
            printf("****Checking threads, current thread is: %d This [i] is: %d\n", currentThread, i);
            struct threadInput thisInput = threadInputList[i];
            if(thisInput.threadComplete == COMPLETE) {
                printf("****Trying to join\n");
                pthread_join(threadList[i], NULL);
                printf("****Completed thread %d\n", thisInput.threadId);
            }    
        }
        printf("****Current thread is %d\n", currentThread);
        //return EXIT_SUCCESS;
    }
    
    // thread close
    //close (sd);
    // server close?
    //close(psd);
}

void* EchoServe(void* inputs) {
    struct threadInput* threadData = (struct threadInput*)inputs;
    int psd = threadData->psd;
    struct sockaddr_in from = threadData->from;
    char buf[512];
    int rc;
    struct  hostent *hp, *gethostbyname();
    
    printf("Serving %s:%d\n", inet_ntoa(from.sin_addr),
	   ntohs(from.sin_port));
    if ((hp = gethostbyaddr((char *)&from.sin_addr.s_addr,
			    sizeof(from.sin_addr.s_addr),AF_INET)) == NULL)
	    fprintf(stderr, "Can't find host %s\n", inet_ntoa(from.sin_addr));
    else
	    printf("(Name is : %s)\n", hp->h_name);
    
    /**  get data from  clients and send it back */
    for(;;){
	    printf("\n...server is waiting...\n");
        if(send(psd, " #\n", 2, 0) <0 )
            perror("sending stream message");
        printf("Sent message...\n");
	    if( (rc=recv(psd, buf, sizeof(buf), 0)) < 0){
	        perror("receiving stream  message");
            threadData->threadComplete = COMPLETE;
	        //exit(-1);
	    }
	    if (rc > 0){
	        buf[rc]='\0';
	        printf("Received: %s\n", buf);
	        printf("From TCP/Client: %s:%d\n", inet_ntoa(from.sin_addr),
		    ntohs(from.sin_port));
	        printf("(Name is : %s)\n", hp->h_name);

	        if (send(psd, buf, rc, 0) <0 )
		        perror("sending stream message");
	    } else {
	        printf("TCP/Client: %s:%d\n", inet_ntoa(from.sin_addr),
		    ntohs(from.sin_port));
	        printf("(Name is : %s)\n", hp->h_name);
	        printf("Disconnected..\n");
	        //close (psd);
            threadData->threadComplete = COMPLETE;
	        return inputs;
	    }
    }
    threadData->threadComplete = COMPLETE;
    return inputs;
}

void reusePort(int s)
{
    int one=1;
    
    if ( setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(char *) &one,sizeof(one)) == -1 )
	{
	    printf("error in setsockopt,SO_REUSEPORT \n");
	    exit(-1);
	}
}      
