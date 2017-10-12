#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <netdb.h> 
#include <string.h> 
#include <unistd.h> 
#include <stdlib.h> 
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/un.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <semaphore.h>



#define MAXHOSTNAME 80
#define MAX_BUFFER 2000
#define MAX_THREADS 50
#define true 1
#define false 0
#define COMPLETE true
#define RUNNING false

extern int errno;
extern char** environ;

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
    FILE* log2;
};

struct Command 
{
    char* isRunning;
    int pid;
    bool isForeground;
    bool mostRecent;
    char** cmd;
    int numCmds;
    char* outfile;
    char* infile;
    struct Command* nextCommand;
  
};

struct Job
{
    char* cmd;
    int id;
    int pid;
    bool isMostRecent;
    bool isRunning;
    struct Job* nextJob;
};

void runInputLoop(char*);
struct Command* parseInput(char*, char*, bool*);
void handleSignal(int);
char* trimTrailingWhitespace(char*);
struct Command createCommand(char*);
void* removeExcess(char**, int);
char** getTokenizedList(char*, char*, int*);
int getFileD(char*, char**, int, bool);
void printJobs(struct Job*,int);
void pushJob(struct Job*, char*, bool, bool, int, int);
int removeJob(struct Job*, int);
int removeLastJob(struct Job*);
int updatePID(struct Job*, int);
int foreground();
int background(int);
bool resetMostRecent(struct Job*);
void printJob(struct Job*, int, bool, int);
void printDoneJob(char*,int,int);
void reusePort(int sock);
void setTimeout(int);
void* EchoServe(void*);
void* yash(void*);
char* parseCommand(char *);
void writeLog(FILE*, char*, char*, int);

pid_t currentChildPID=-1;
char* currentCmd = NULL;
bool currentHasPipe = false;
int lastRemovedJobId = -1;
int jobsSize = 1;
struct Job* jobs;
sem_t logSemaphore;
static FILE *log;
 

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

  //fprintf(stderr, "Child terminated\n");
  //wait(&status); /* So no zombies */
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
    FILE *log2;

    int ret = sem_init(&logSemaphore, 0, 1);
    if (ret != 0) {
        perror("Unable to initialize the semaphore");
        abort();
    } 

    if (argc > 1) {
        printf("Argc is greater than one\n");
        printf("U server path is: %s", u_server_path);
        strncpy(u_server_path, argv[1], PATHMAX); /* use argv[1] */
    }  
    strncat(u_server_path, "/", PATHMAX-strlen(u_server_path));
    strcpy(u_log_path, u_server_path);
    strncat(u_server_path, argv[0], PATHMAX-strlen(u_server_path));
    strncat(u_log_path, "yash_e_log", PATHMAX-strlen(u_log_path));
    strcpy(u_pid_path, u_server_path);
    strncat(u_pid_path, ".pid", PATHMAX-strlen(u_pid_path));

    strncat(u_log_path, ".log", PATHMAX-strlen(u_log_path));

    daemon_init(u_server_path, 0); /* We stay in the u_server_path directory and file
                                    creation is not restricted. */

    log2 = fopen("/tmp/yashd.log", "aw"); 
    if(log2 == NULL) {
        fprintf(stderr, "ERROR WRITING FILE!!!!!!!!!!!!!!!!!!!!\n");
    }    
    sp = getservbyname("echo", "tcp");
    /* get TCPServer1 Host information, NAME and INET ADDRESS */

    gethostname(ThisHost, MAXHOSTNAME);
    /* OR strcpy(ThisHost,"localhost"); */
    
    fprintf(stderr, "----TCP/Server running at host NAME: %s\n", ThisHost);
    if  ( (hp = gethostbyname(ThisHost)) == NULL ) {
        fprintf(stderr, "Can't find host %s\n", argv[1]);
        exit(-1);
    }
    bcopy ( hp->h_addr, &(server.sin_addr), hp->h_length);
    fprintf(stderr, "    (TCP/Server INET ADDRESS is: %s )\n", inet_ntoa(server.sin_addr));

    
    
    /** Construct name of socket */
    server.sin_family = AF_INET;
    /* OR server.sin_family = hp->h_addrtype; */
    
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    if (argc == 1) {
        fprintf(stderr, "The arg count is 1\n");
        //server.sin_port = htons(0); 
        server.sin_port = htons(3826); 
    }
    else  {
        fprintf(stderr, "No arguments required\n");
        exit(1);
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
    //setTimeout(sd);
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
    
    listen(sd,4);
    fromlen = sizeof(from);


    for(;;){


        fprintf(stderr, "****Waiting on new connection....\n");
	    psd  = accept(sd, (struct sockaddr *)&from, &fromlen);
        fprintf(stderr, "****Got connection\n");
        struct threadInput argin = {psd, from, RUNNING,0, log2};
        threadInputList[currentThread] = argin;
        if ((returnCode = pthread_create(&threadList[currentThread], NULL, yash, &threadInputList[currentThread]))) {
            fprintf(stderr, "****error: pthread_create, returnCode: %d\n", returnCode);
            return EXIT_FAILURE;
        }
        currentThread++;

        /* block until all threads complete */
        for (int i = 0; i < currentThread; ++i) {
            //fprintf(stderr, "****Checking threads, current thread is: %d This [i] is: %d\n", currentThread, i);
            struct threadInput thisInput = threadInputList[i];
            if(thisInput.threadComplete == COMPLETE) {
                fprintf(stderr, "****Trying to join\n");
                pthread_join(threadList[i], NULL);
                fprintf(stderr, "****Completed thread %d\n", thisInput.threadId);
            }    
        }
        //fprintf(stderr, "****Current thread is %d\n", currentThread);
        //return EXIT_SUCCESS;
    }    
    fclose(log2); 
    
    // thread close
    //close (sd);
    // server close?
    //close(psd);
}



void* yash(void* inputs) {
    struct threadInput* threadData = (struct threadInput*)inputs;
    int psd = threadData->psd;
    struct sockaddr_in from = threadData->from;
    char* buf;
    char recBuf[MAX_BUFFER];
    int rc;
    struct  hostent *hp, *gethostbyname();
    FILE* cmdLog = threadData->log2;

    jobs = malloc(sizeof(struct Job));

    fprintf(stderr, "Serving %s:%d\n", inet_ntoa(from.sin_addr),
       ntohs(from.sin_port));
    if ((hp = gethostbyaddr((char *)&from.sin_addr.s_addr,
                sizeof(from.sin_addr.s_addr),AF_INET)) == NULL)
        fprintf(stderr, "Can't find host %s\n", inet_ntoa(from.sin_addr));
    else
        fprintf(stderr, "(Name is : %s)\n", hp->h_name);
            
    while(1)
    { 
        //buf = malloc(sizeof(char) * MAX_BUFFER);
        
        char* printBuf = NULL; 
        printBuf = malloc(sizeof(char) * MAX_BUFFER);
        bool haspipe = false;
        
        int fd1, fd2, status;
        int pipefd[2];
        int numPaths = 0;

        char* path = getenv("PATH");
        char* currpath = malloc(strlen(path));
        strcpy(currpath, path);
        char** paths = getTokenizedList(currpath, ":", &numPaths);
        //fflush(stdin);
        fprintf(stderr, "\n...server is waiting...\n");
        if(send(psd, " #\n", 2, 0) <0 )
            perror("sending stream message");
        fprintf(stderr,"Sent message...\n");
        char* env_list[] = {};

        if( (rc=recv(psd, recBuf, sizeof(recBuf), 0)) < 0){
            perror("receiving stream  message");
            //TODO: take out?
            threadData->threadComplete = COMPLETE;
            //exit(-1);
        }
        for(int i = 0; i<strlen(recBuf); i++) {
          if(recBuf[i] == '\n') {
            recBuf[i] = ' ';
            break;
          }
        }
        if (rc > 0){
            recBuf[rc]='\0';
            fprintf(stderr, "Received: %s\n", recBuf);
            fprintf(stderr, "From TCP/Client: %s:%d\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));
            fprintf(stderr,"(Name is : %s)\n", hp->h_name);
       

            if(recBuf == NULL) {
                fprintf(stderr,"There was an error with the input.\n");
                if(send(psd, "There was an error with the input.\n", 34, 0) <0 )
                    perror("sending stream message");
                fprintf(stderr,"Sent message...\n");
                continue;
            }
            if(strcmp(recBuf,"") == 0)
                continue;

            buf = parseCommand(recBuf);
            fprintf(stderr, "Buf is: %s\n", buf);
            //fprintf(stderr, "sin_addr: %s\n",from.sin_addr);
            //fprintf(stderr, "sin_port: %s\n", from.sin_port);
            if(strcmp(buf,"Ctrl-c\n") == 0 || strcmp(buf,"Ctrl-z\n") == 0) {
                continue;
            }
            writeLog(cmdLog, buf, inet_ntoa(from.sin_addr),ntohs(from.sin_port));
            if(strcmp(buf,"jobs ") == 0) {
                fprintf(stderr, "GOT JOBS CMD\n");
                printJobs(jobs,psd);
                continue;
            } else if(strcmp(buf,"bg ") == 0) {
                background(psd);
                continue;
            } else if(strcmp(buf, "fg ") == 0) {
                pid_t ret2 = foreground();
                int checkpid = waitpid(ret2, &status, WUNTRACED|WNOHANG);
                while(1) {
                    fprintf(stderr," WAITING ON PID FG\n");
                    if(checkpid == ret2) {
                        if(WIFEXITED(status)) {                     
                            printDoneJob(currentCmd,lastRemovedJobId,psd); 
                            break;
                        } else if (WIFSTOPPED(status)) {
                            break;
                        } else if (WIFSIGNALED(status)) {
                            if(WTERMSIG(status) == SIGINT) {
                                printDoneJob(currentCmd,lastRemovedJobId, psd);    
                                break;
                            }   
                        }
                    }

                    if( (rc=recv(psd, recBuf, sizeof(recBuf), MSG_DONTWAIT)) < 0){
                        //perror("ERROR receiving stream  message");
                        //TODO: take out?
                        //exit(-1);
                    }  
                    if(rc > 0) {
                        fprintf(stderr," GOT A CONTROL\n");
                        if(strstr(recBuf, "CTL") == &recBuf[0]){
                            if(strstr(recBuf,"c") == &recBuf[4] ) {
                                //Ctrl-c - To stop the current running command (on the server)
                                fprintf(stderr," GOT A CONTROL, sending SIGSTOP to PID: %d\n", ret2);
                                kill(ret2, SIGKILL);
                            } else if (strstr(recBuf,"z") == &recBuf[4]) {
                                //Ctrl-z  - To suspend the current running command (on the server)
                                kill(ret2, SIGINT);   
                                fprintf(stderr," GOT A CONTROL, sending SIGINT to PID: %d\n", ret2);                            
                            }
                        } 
                        break;                           
                    } 
                    checkpid = waitpid(ret2, &status, WUNTRACED|WNOHANG);
                } 
                continue;
            }
            strcpy(printBuf,buf);
            fprintf(stderr, "Printbuf for background: %s\n", printBuf);
            char* buf2 = "";
            buf2 = malloc(sizeof(char) * strlen(buf));
            struct Command* thisCommand = parseInput(buf, buf2, &haspipe);
            if(haspipe) {
                currentHasPipe = true;
            }

            if(thisCommand == NULL) {
                fprintf(stderr,"There was an error with the input.\n");
                if(send(psd, "There was an error with the input.\n", 34, 0) <0 )
                    perror("sending stream message");
                fprintf(stderr,"Sent message...\n");
                continue;
            }


            if(!thisCommand->isForeground) {
                fprintf(stderr,"This is background job pushing printBuf: %s\n", printBuf);
                pushJob(jobs,printBuf,true,true,jobsSize,jobsSize);
                jobsSize++;
            } else {
                currentCmd = printBuf;
            }    

            if(haspipe) {
                if(pipe(pipefd) == -1) {
                    perror("pipe");
                    exit(-1);
                }
            }

            pid_t ret3 = fork();
            currentChildPID = ret3;

            if (ret3 == 0) 
            {

                if(haspipe) {
                    dup2(pipefd[0],0);
                    close(pipefd[1]);
                    currentHasPipe = true;

                } else if(strlen(thisCommand[0].infile) > 0) {
                    fd2 = getFileD(thisCommand[0].infile, paths, numPaths, false);
                    if(fd2 == -1) {
                        printf("There was an error opening the input file.\n");
                        if(send(psd, "There was an error opening the input file.\n", 43, 0) <0 )
                            perror("sending stream message");
                        fprintf(stderr,"Sent message...\n");                   
                        // TODO: make sure jobis not in jobs list
                        continue;
                    } else {
                        dup2(fd2,0);
                    }
                } 


                if(strlen(thisCommand[0].outfile) > 0) {
                    fd1 = getFileD(thisCommand[0].outfile, paths, numPaths, true);
                    if(fd1 == -1)
                    {
                        if(send(psd, "There was an error opening or creating the out file.\n", 52, 0) <0 )
                            perror("sending stream message");
                        printf("There was an error opening or creating the out file.\n");
                        fprintf(stderr,"Sent message...\n");
                    } else {
                        dup2(fd1,1);
                    }   
                } else {
                    //fprintf(stderr, "%%%%%%duping psd to stdout\n");
                    dup2(psd,1);
                }


                //printf("READER calling exec: %s\n", thisCommand[0].cmd[0]);
                fprintf(stderr, "Numcmds for cmdArray is: %d", thisCommand[0].numCmds);
                char* cmdArray2[thisCommand[0].numCmds+1];
                for (int i=0; i<thisCommand[0].numCmds; i++) {
                    cmdArray2[i] = thisCommand[0].cmd[i];
                    fprintf(stderr,"Creating cmd array %s\n", cmdArray2[i]);
                }
                cmdArray2[thisCommand[0].numCmds] = NULL;
                fprintf(stderr, "Calling exec 1:%s:: Array[0]:%s:: Array[1]::%s:: Array[2]::%s::\n", thisCommand[0].cmd[0], cmdArray2[0], cmdArray2[1], cmdArray2[2]);
                int execResult = execvpe(thisCommand[0].cmd[0], cmdArray2, environ);
                //TODO: do we ever execut here?
                printf("There was an error with the command %d\n!", execResult);

                switch(errno)
                {
                    case E2BIG:
                        printf("ERRORIS: E2BIG\n");
                    break;
                    case EACCES:
                        printf("ERRORIS: EACCES\n");
                    break;
                    case EFAULT:
                        printf("ERRORIS: EFAULT\n");
                    break;
                    case EINVAL:
                        printf("ERRORIS: EINVAL\n");
                    break;
                    case EIO:
                        printf("ERRORIS: EIO\n");
                    break;
                    case EISDIR:
                        printf("ERRORIS: EISDIR\n");
                    break;
                    case ELIBBAD:
                        printf("ERRORIS: ELIBBAD\n");
                    break;
                    case ELOOP:
                        printf("ERRORIS: ELOOP\n");
                    break;
                    case EMFILE:
                        printf("ERRORIS: EMFILE\n");
                    break;
                    case ENAMETOOLONG:
                        printf("ERRORIS: ENAMETOOLONG\n");
                    break;
                    case ENFILE:
                        printf("ERRORIS: ENFILE\n");
                    break;
                    case ENOENT:
                        printf("ERRORIS: ENOENT\n");
                    break;
                    case ENOEXEC:
                        printf("ERRORIS: ENOEXEC\n");
                    break;
                    case ENOTDIR:
                        printf("ERRORIS: ENOTDIRv\n");
                    break;
                    case EPERM:
                        printf("ERRORIS: EPERM\n");
                    break;
                    case ETXTBSY:
                        printf("ERRORIS: ETXTBSY\n");
                    break;
                }
                if (fd1 != -1) close(fd1);
                if (fd2 != -1) close(fd2);
                exit(1);
            } else if (ret3 < 0) 
            {

            } else if (haspipe) {

                pid_t ret = fork();
                
                if(ret ==0) { 
                    
                    if(haspipe) {
                        currentHasPipe = true;
                        dup2(pipefd[1],1);
                        close(pipefd[0]);

                    } else if(strlen(thisCommand[1].outfile) > 0) {

                        fd1 = getFileD(thisCommand[1].outfile, paths, numPaths, true);
                        if(fd1 == -1)
                        {
                            if(send(psd, "There was an error opening or creating the out file.\n", 52, 0) <0 )
                                perror("sending stream message");
                            printf("There was an error opening or creating the out file.\n");
                            fprintf(stderr,"Sent message...\n");
                        } else {
                            dup2(fd1,1);
                        }
                    } else {
                    //fprintf(stderr, "%%%%%%duping psd to stdout\n");
                        dup2(psd,1);
                    }


                    if(strlen(thisCommand[1].infile) > 0) {

                        fd2 = getFileD(thisCommand[1].infile, paths, numPaths, false);
                        if(fd2 == -1) {
                            printf("There was an error opening the input file.\n");
                            if(send(psd, "There was an error opening the input file.\n", 43, 0) <0 )
                                perror("sending stream message");
                            fprintf(stderr,"Sent message...\n");
                            // TODO: make sure jobis not in jobs list
                            continue;
                        } else {
                            dup2(fd2,0);
                        }
                    }
                    fprintf(stderr, "Numcmds for cmdArray is: %d", thisCommand[1].numCmds);
                    char* cmdArray[thisCommand[1].numCmds+1];
                    for (int i=0; i<thisCommand[1].numCmds; i++) {
                        cmdArray[i] = thisCommand[1].cmd[i];
                        fprintf(stderr, "Creating command array: %s\n", cmdArray[i]);
                    }
                    cmdArray[thisCommand[1].numCmds] = NULL;
                    fprintf(stderr, "Calling exec 2::%s:: Array[0]::%s:: Array[1]::%s::\n", thisCommand[1].cmd[0], cmdArray[0], cmdArray[1]);
                    execvpe(thisCommand[1].cmd[0], cmdArray, environ);
                    printf("There was an error with the command!\n");
                    if (fd1 != -1) close(fd1);
                    exit(2);

                } else {

                    close(pipefd[1]);
                    if(thisCommand[1].isForeground) {
                        //TODO: does this do anything?
                        //printf("This is a forground task\n");
                        //printf("Waiting on PID: %d\n", ret);
                        int checkpid = waitpid(ret, &status, WUNTRACED);
                        while(checkpid != ret && (!WIFSTOPPED(status) && !WIFEXITED(status))) {
                            fprintf(stderr," WAITING ON PID 2\n");
                            checkpid = waitpid(ret, &status, WUNTRACED);
                        }
                        //printf("WIFSTOPPED1: %d\n", WIFSTOPPED(status));
                        //printf("WIFEXITED1: %d\n", WIFEXITED(status));
                    } else {
                        //printf("This is a background task\n");
                        continue;
                    }
                    
                }
                
            }
            if(thisCommand[0].isForeground) {

                int checkpid2 = waitpid(ret3, &status, WUNTRACED|WNOHANG);
                //printf("Waiting on PID: %d check pid: %d\n", ret3, checkpid2);
                while(1) {
                   //printf("In loop\n");
                    //fprintf(stderr," WAITING ON PID 3\n");                  
                    if(checkpid2 == ret3) {
                        if(WIFEXITED(status)) {
                            //printf("Exited, exit status %d\n", WEXITSTATUS(status));
                            printJob(jobs,ret3,true, psd); 
                            break;
                        } else if (WIFSTOPPED(status)) {
                            break;
                        } else if (WIFSIGNALED(status)) {
                            //printf("Signal is: %d\n", WTERMSIG(status));
                            if(WTERMSIG(status) == SIGINT) {
                                printJob(jobs,ret3,true, psd);   
                                break;
                            }   
                        }
                    } 
                    if( (rc=recv(psd, recBuf, sizeof(recBuf), MSG_DONTWAIT)) < 0){
                        //perror("ERROR receiving stream  message");
                        //TODO: take out?
                        //exit(-1);
                    }  
                    if(rc > 0) {
                        fprintf(stderr," GOT A CONTROL\n");
                        if(strstr(recBuf, "CTL") == &recBuf[0]){
                            if(strstr(recBuf,"c") == &recBuf[4] ) {
                                //Ctrl-c - To stop the current running command (on the server)
                                fprintf(stderr," GOT A CONTROL, sending SIGSTOP to PID: %d\n", ret3);
                                kill(ret3, SIGKILL);
                            } else if (strstr(recBuf,"z") == &recBuf[4]) {
                                //Ctrl-z  - To suspend the current running command (on the server)
                                kill(ret3, SIGINT);   
                                fprintf(stderr," GOT A CONTROL, sending SIGINT to PID: %d\n", ret3);                            
                            }
                        } 
                        break;                           
                    }
                    checkpid2 = waitpid(ret3, &status, WUNTRACED|WNOHANG);
                }

                if(WIFEXITED(status)) {
                    //set current job ID?
                } 

                currentHasPipe = false;   

            } else {
                updatePID(jobs, ret3);
                continue;
            }   
        } else {
            fprintf(stderr, "TCP/Client: %s:%d\n", inet_ntoa(from.sin_addr),
            ntohs(from.sin_port));
            fprintf(stderr,"(Name is : %s)\n", hp->h_name);
            fprintf(stderr, "Disconnected..\n");
            //close (psd);
            threadData->threadComplete = COMPLETE;
            return inputs;            
        }   
        free(printBuf); 
        free(buf);
        free(currpath);
    }
    threadData->threadComplete = COMPLETE;
    return inputs;
}  

void pushJob(struct Job* head, char* thisCmd, bool isRun, bool isRecent, int size, int pid)
{

    struct Job* current = head;
    fprintf(stderr, "Pushing job for: %s\n", thisCmd);
    if(current != NULL) {
        while(current->nextJob != NULL) {
            current->isMostRecent = false;
            current = current->nextJob;
        }
        current->isMostRecent = false;
        current->nextJob = malloc(sizeof(struct Job));
        current->nextJob->cmd = thisCmd;
        current->nextJob->id = size;
        current->nextJob->pid = pid;
        current->nextJob->isRunning = isRun;
        current->nextJob->isMostRecent = isRecent;
        current->nextJob->nextJob =NULL;
    } 

}

int removeJob(struct Job* jobsList, int pid) {
    struct Job* current = jobsList;
    struct Job* previous = NULL;
    while(current != NULL) {
        if(current->pid == pid)
        {
            previous->nextJob = current->nextJob;
            lastRemovedJobId = current->id;
            free(current);
            resetMostRecent(jobsList);
            return pid;
        }
        previous=current; 
        current = current->nextJob;   
    }
    return -1;

}

bool resetMostRecent(struct Job* jobsList) {
    struct Job* current = jobsList;
    struct Job* previous = NULL;
    while(current != NULL) {
        if(current->isMostRecent == true)
        {
            return true;
        }
        previous=current; 
        current = current->nextJob;   
    }
    if(previous != NULL) {
        previous->isMostRecent = true;
        return true;
    } else {
        return false;
    }
}

int removeLastJob(struct Job* jobsList) {
    struct Job* current = jobsList;
    struct Job* previous = NULL;
    while(current != NULL) {
        previous=current;
        if(current->nextJob == NULL) {
            int pid = current->pid;
            free(current);
            previous->nextJob = NULL;
            return pid;
        } 
        current = current->nextJob; 
    }
    return -1;
}
struct Command* parseInput(char* buf, char* buf2, bool* haspipe)
{
   
    int position = 0;
    buf2 = malloc(sizeof(char*) * strlen(buf));
    fprintf(stderr, "starting parse input, original buf: %s\n and buf2: %s\n", buf, buf2);
    unsigned long count = strlen(buf);
    fprintf(stderr, "The stringlenth of buf is: %lu\n", count);
    for(unsigned long i=0; i < count; i++) {
        if(buf[i] == '|' && !*haspipe) {
            fprintf(stderr, "Buf i was pipe and we just set pipe to true. Assigning buf[%lu]: bs 0 incrementing i and setting next to bs 0\n", i);
            buf[i] = '\0';
            *haspipe = true;
            i++;
            buf[i] = '\0';
        } else if(buf[i] == '|' && *haspipe) {
            fprintf(stderr, "Found a second pipe returnin NULL\n");
            return NULL;
        } else if (*haspipe) {
            fprintf(stderr, "Now we have a pipe assigning buf2[%d]::%c:: and incrementing position from %d setting buf[%lu] to bs 0\n", position, buf[i], position, i);
            buf2[position] = buf[i];
            position++;
            buf[i] = '\0';
        }

    }
    fprintf(stderr, "starting parse input, original buf: %s\n and buf2: %s\n", buf, buf2);
    buf = trimTrailingWhitespace(buf);
    if (*haspipe) {
        buf2 = trimTrailingWhitespace(buf2);
    }
    fprintf(stderr, "starting parse input, original buf: %s\n and buf2: %s\n", buf, buf2);
    fprintf(stderr, "ParseInput::Calling create command on buf::%s::\n",buf);
    struct Command retCmd = createCommand(buf);
    //TODO - start here with debug....
    // Restrict pipe and bg
    if(retCmd.isForeground == false && *haspipe) return NULL;
    if(retCmd.cmd == NULL)  return NULL;
    struct Command* cmdList;
    struct Command retcmd2;
    fprintf(stderr, "starting parse input, original buf: %s\n and buf2: %s\n", buf, buf2);
    if(*haspipe) {
        cmdList = malloc(2 * sizeof(struct Command));
        cmdList[1] = retCmd;
        fprintf(stderr, "Starting create command on buf2::%s::\n", buf2);
        retcmd2 = createCommand(buf2);
        // Restrict pipe and bg
        if(retcmd2.isForeground == false && *haspipe) return NULL;    
        cmdList[0] = retcmd2;
    } else {
        cmdList = malloc(sizeof(struct Command));
        cmdList[0] = retCmd;
    }   

    return cmdList; 

}

char* trimTrailingWhitespace(char* thisbuf) {
    char *end;

    end = thisbuf + strlen(thisbuf) - 1;
    while(end > thisbuf && *end == ' ') end--;
    *(end+1) = 0;

    return thisbuf;
}

void* removeExcess(char** buf, int trimNum) {
    if (trimNum == 0) {
        return NULL;
    }

    char** cmds = malloc(sizeof(char*) * trimNum);
    memcpy(cmds, buf, (sizeof(char*) * trimNum));

    return cmds;
}

struct Command createCommand(char* buf) {
    //printf("This starting buffer is: %s\n", buf);
    int numCmds = 1;
    bool isFor = true;
    char* isRunning = "Stopped";
    fprintf(stderr,"Starting create command%s\n", buf);
    // Find number of strings in this array
    for(int i=0; i<strlen(buf); i++) {
        if(buf[i] == ' ' || buf[i] == '\0') 
        {
            numCmds++;
        }
    }
    // Get all the strings divided by spaces
    fprintf(stderr,"Finished figuring out commands, numCMds is: %d\n", numCmds);
    char** cmds = malloc(sizeof(char*) * numCmds);
    char* token = strtok(buf, " ");
    int position = 0;
    fprintf(stderr,"About to tokenize\n");
    while(token) {
        fprintf(stderr, "%s\n", token);
        cmds[position] =token;
        position++;
        token = strtok(NULL, " ");
    }
    // Figure if this command will be a background process
    char* infile = "";
    char* outfile = "";
    for(int i=0; i < numCmds; i++) {
        fprintf(stderr, "In first for loop \n");
        if((strcmp(cmds[i],"&") == 0) && isFor) {
            fprintf(stderr, "In if\n");
            isFor = false;
            numCmds--;
            if(i<numCmds) {
                struct Command thisCommand = {false, -1, false, true, NULL, -1, NULL, NULL};
                return thisCommand;
            }
            break;
        } else {
            //fprintf(stderr, "finished string cmp\n");
        } 
    }   

    // Get in and outfiles
    int lastCmd = 0;
    for(int i=0; i<numCmds; i++) {
       if (strcmp(cmds[i], "<") == 0) {
            infile = cmds[i + 1];
            //printf("Found <\n");
            if(lastCmd == 0) lastCmd = i;
        } else if (strcmp(cmds[i], ">") == 0) {
            outfile = cmds[i + 1];
            //printf("Found >\n");
            if(lastCmd == 0) lastCmd = i;
        } 
    }   
    if (lastCmd < numCmds && lastCmd != 0) numCmds = lastCmd;
    cmds = removeExcess(cmds, numCmds);
    struct Command thisCommand = {isRunning, -1, isFor, true, cmds, numCmds, outfile, infile};

    return thisCommand;
}

int getFileD(char* file, char** paths, int num, bool create) {

    int filed ;
    if (access(file, F_OK) != -1) {
        filed = open(file, O_RDWR|O_CREAT, 0777);
        return filed;
    }
    char* filepath;
    for(int i=0; i< num; i++) {
        filepath = malloc(strlen(paths[i]) + strlen(file) + 2);
        strcpy(filepath, paths[i]);
        strcat(filepath, "/");
        strcat(filepath, file);
        if(access(filepath, F_OK) != -1) 
        {
            filed = open(filepath, O_RDWR|O_CREAT, 0777);
            return filed;
        }
    }
    if(create)
    {   
        filed = open(file, O_RDWR|O_CREAT, 0777);
        return filed;
    }    
    return -1;

}

int updatePID(struct Job* jobsList, int pid) {
    struct Job* current = jobsList;
    struct Job* previous = NULL;
    while(current != NULL) {
        previous=current;
        if(current->nextJob == NULL) {
            current->pid = pid;
            return pid;
        } 
        current = current->nextJob; 
    }
    return -1;  
}

void printJobs(struct Job* jobsList, int psd) {

    struct Job* current = jobsList;
    while(current != NULL) {
        if(current->cmd != NULL)
        {
            char* msg;
            asprintf(&msg, "[%d]", current->id);
            if(send(psd, msg, strlen(msg), 0) <0 )
                perror("sending stream message");
            if(current->isMostRecent) {
                asprintf(&msg, " +");
                if(send(psd, msg, strlen(msg), 0) <0 )
                    perror("sending stream message");
            } else {
                asprintf(&msg, " -");
                if(send(psd, msg, strlen(msg), 0) <0 )
                    perror("sending stream message");
            }
            if(current->isRunning) { 
                asprintf(&msg, " Running %s\n", current->cmd);
                if(send(psd, msg, strlen(msg), 0) <0 )
                    perror("sending stream message");
            } else {
                asprintf(&msg, " Stopped %s\n", current->cmd);
                if(send(psd, msg, strlen(msg), 0) <0 )
                    perror("sending stream message");
            }
        } 
        current = current->nextJob;   
    }
}

void printJob(struct Job* jobsList, int pid, bool jobDone, int psd) {

    struct Job* current = jobsList;
    while(current != NULL) {
        if(current->cmd != NULL)
        {
            if(current->pid == pid) {
                char* msg;
                asprintf(&msg,"[%d]", current->id);
                if(send(psd, msg, strlen(msg), 0) <0 )
                    perror("sending stream message");
                fprintf(stderr,"Sent message...\n");
                if(current->isMostRecent) { 
                    asprintf(&msg, " +");
                    if(send(psd, msg, strlen(msg), 0) <0 )
                        perror("sending stream message");
                    fprintf(stderr,"Sent message...\n");
                } else {
                    asprintf(&msg, " -");
                    if(send(psd, msg, strlen(msg), 0) <0 )
                        perror("sending stream message");
                    fprintf(stderr,"Sent message...\n");
                }
                if(current->isRunning) {
                    asprintf(&msg, " Running ");
                    if(send(psd, msg, strlen(msg), 0) <0 )
                        perror("sending stream message");
                    fprintf(stderr,"Sent message...\n");
                } else if (jobDone) {
                    asprintf(&msg, " Done ");
                    if(send(psd, msg, strlen(msg), 0) <0 )
                        perror("sending stream message");
                    fprintf(stderr,"Sent message...\n");
                } else {
                    asprintf(&msg, " Stopped ");
                    if(send(psd, msg, strlen(msg), 0) <0 )
                        perror("sending stream message");
                    fprintf(stderr,"Sent message...\n");
                }
                asprintf(&msg, " %s\n", current->cmd);
                if(send(psd, msg, strlen(msg), 0) <0 )
                    perror("sending stream message");
                fprintf(stderr,"Sent message...\n");
                //printf(" PID: %d\n", current->pid);
                return;
            }    
        } 
        current = current->nextJob;   
    }
}

void printDoneJob(char* cmd,int jobId, int psd) {
    char * msg;
    asprintf(&msg, "\n[%d] + Done  %s\n", jobId, cmd);
    if(send(psd, msg, strlen(msg), 0) <0 )
        perror("sending stream message");
    fprintf(stderr,"Sent message...\n");
}
int foreground(int psd) 
{
    struct Job* current = jobs;
    while(current != NULL) {
        if(current->cmd != NULL) 
        {
            if(current->isMostRecent) 
            {

                currentCmd = current->cmd;
                char* msg;
                asprintf(&msg, "%s\n", currentCmd);
                if(send(psd, msg, strlen(msg), 0) <0 )
                    perror("sending stream message");
                fprintf(stderr,"Sent message...\n");
                currentChildPID = current->pid;
                lastRemovedJobId = current->id;
                removeJob(jobs,current->pid);
                kill(current->pid, SIGCONT);
                return current->pid;
            }               
        } 
        current = current->nextJob;   
    }

    return -1;
}

int background(int psd) 
{
    struct Job* current = jobs;
    while(current != NULL) {
        if(current->cmd != NULL) 
        {
            if(current->isMostRecent) 
            {
                if(!current->isRunning) {
                    currentCmd = current->cmd;
                    //printf("%s\n", currentCmd);
                    currentChildPID = current->pid;
                    current->isRunning = true;
                    printJob(jobs,currentChildPID,false,psd);
                    kill(current->pid, SIGCONT);
                    return current->pid;
                }    
            }               
        } 
        current = current->nextJob;   
    }
    return -1;
}


char** getTokenizedList(char* breakString, char* search, int* numStrings) {

    for(int i=0; i<strlen(breakString); i++) {
        if(breakString[i] == ':') (*numStrings)++;
    }
    if(strlen(breakString) >= 1 && (*numStrings) == 0) 
    {
        (*numStrings)=1;
    }
    else if (strlen(breakString) >= 1)
    {
        (*numStrings)++;
    }    
    char** pathList = malloc(sizeof(char*) * (*numStrings));
    char* token = strtok(breakString, search);
    int position = 0;
    while(token) {
        pathList[position] =token;
        position++;
        token = strtok(NULL, search);
    }
    return pathList;
}

void writeLog(FILE* log, char * commandToLog, char* server, int port) {

    int ret;
    do {
       ret = sem_wait(&logSemaphore);
       if (ret != 0) {
           if (errno != EINVAL) {
                perror(" -- thread A -- Error in sem_wait. terminating -> ");
                pthread_exit(NULL);
           } 
        }
    } while (ret != 0);

    char formatTime[20];
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    strftime(formatTime,sizeof(formatTime),"%b %d %T", tm);
    fprintf(log, "%s yashd[%s:%d]: %s\n", formatTime, server, port, commandToLog);
    fprintf(stderr, "WRITING LOG!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    fflush(log);
    ret = sem_post(&logSemaphore);
    if (ret != 0) {
        perror(" -- thread A -- Error in sem_post");
        pthread_exit(NULL);
    }
}

char* parseCommand(char* thisCommand) {

    fprintf(stderr,"Into parse command: %s\n", thisCommand);
    fprintf(stderr,"thisCommand[0]: %s\n", &thisCommand[0]);
    fprintf(stderr,"Into parse command: %s\n", &thisCommand[4]);
    char* retString = "";

    if(strstr(thisCommand, "CMD") == &thisCommand[0]) {
        fprintf(stderr,"Parse command allocating size %lu\n", strlen(thisCommand)-4);
        retString = malloc(sizeof(char*) * (strlen(thisCommand) -4));
        memcpy(retString, &thisCommand[4], (sizeof(char*) * (strlen(thisCommand) -4)));
        fprintf(stderr,"Return string is: %s\n", retString);
    } else if(strstr(thisCommand, "CTL") == &thisCommand[0]){
        if(strstr(thisCommand,"c") == &thisCommand[4] ) {
            retString = "Ctrl-c\n";
            //Ctrl-c - To stop the current running command (on the server)
            //kill(getpid(), SIGSTOP);
        } else if (strstr(thisCommand,"z") == &thisCommand[4]) {
            //Ctrl-z  - To suspend the current running command (on the server)
            retString = "Ctrl-z\n";
        } else {
            retString = "Unknown command\n";
        }
    } else {
        retString = "Unknown command\n";
    }
    fprintf(stderr, "Ret string::%s::\n", retString);
    return retString;
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

void setTimeout(int s)
{
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    
    if ( setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char *) &tv, sizeof(struct timeval)) == -1 )
    {
        fprintf(stderr, "error in setsockopt,SO_RCVTIMEO \n");
        exit(-1);
    }
}