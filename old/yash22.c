/********************************************************************************
** yash.c
** Amy Reed
** UTEID: alr2434
** References: 	https://brennan.io/2015/01/16/write-a-shell-in-c/
**				
*********************************************************************************/
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MAX_BUFFER 200
#define true 1
#define false 0

extern char** environ;

typedef int bool;

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
char* collectInput(struct Job*);
void handleSignal(int);
char* trimTrailingWhitespace(char*);
struct Command createCommand(char*);
void* removeExcess(char**, int);
char** getTokenizedList(char*, char*, int*);
int getFileD(char*, char**, int, bool);
void printJobs(struct Job*);
void pushJob(struct Job*, char*, bool, bool, int, int);
int removeJob(struct Job*, int);
int removeLastJob(struct Job*);
int updatePID(struct Job*, int);
void stopAllJobs(struct Job*);
int foreground();
int background();
bool resetMostRecent(struct Job*);
void printJob(struct Job*, int, bool);
void printDoneJob(char*,int);

pid_t currentChildPID=-1;
char* currentCmd = NULL;
bool currentHasPipe = false;
int lastRemovedJobId = -1;
int jobsSize = 1;
struct Job* jobs;

int main(int argc, char** args) 
{
    char buf[MAX_BUFFER];

    const char* path = getenv("PATH");
    jobs = malloc(sizeof(struct Job));


    signal(SIGTSTP, &handleSignal);
    signal(SIGCHLD, &handleSignal);
    signal(SIGINT, &handleSignal);
    runInputLoop(buf);

}

void runInputLoop(char* buf ) {

	fflush(stdin);
	fflush(stdout);


    while(1)
    { 
    	buf = malloc(sizeof(char) * MAX_BUFFER);
  	    char* buf2 = malloc(sizeof(char) * MAX_BUFFER);
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
        printf("# ");
        char* env_list[] = {};

        buf = collectInput(jobs);
        if(buf == NULL) {
        	printf("There was an error with the input.\n");
        	continue;
        }
        if(strcmp(buf,"") == 0)
        	continue;
        strcpy(printBuf,buf);
        
        struct Command* thisCommand = parseInput(buf, buf2, &haspipe);
        if(haspipe) {
        	currentHasPipe = true;
        }

        if(thisCommand == NULL) {
        	printf("There was an error with the command.\n");
        	continue;
        }
        if(strcmp(buf,"jobs") == 0) {
        	printJobs(jobs);
        	continue;
        } else if(strcmp(buf,"bg") == 0) {
        	background();
        	continue;
        } else if(strcmp(buf, "fg") == 0) {
        	pid_t ret2 = foreground();
            int checkpid = waitpid(ret2, &status, WUNTRACED);
            while(1) {
                if(checkpid == ret2) {
                	if(WIFEXITED(status)) {              		
                		printDoneJob(currentCmd,lastRemovedJobId); 
                		break;
                	} else if (WIFSTOPPED(status)) {
                		break;
                	} else if (WIFSIGNALED(status)) {
                        if(WTERMSIG(status) == SIGINT) {
                        	printDoneJob(currentCmd,lastRemovedJobId);    
                        	break;
                        }	
                	}
                } 
            	checkpid = waitpid(ret2, &status, WUNTRACED);
            } 
        	continue;
        }

        if(!thisCommand->isForeground) {
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
        			printf("There was an error opening or creating the out file.\n");
        		} else {
        			dup2(fd1,1);
        		}	
        	}


        	//printf("READER calling exec: %s\n", thisCommand[0].cmd[0]);
        	char* cmdArray[thisCommand[0].numCmds+1];
        	for (int i=0; i<thisCommand[0].numCmds; i++) {
        		cmdArray[i] = thisCommand[0].cmd[i];
        	}
            cmdArray[thisCommand[0].numCmds] = NULL;
            printf("READER calling exec: ::%s::\n", thisCommand[0].cmd[0]);
        	execvpe(thisCommand[0].cmd[0], cmdArray, environ);
            //TODO: do we ever execut here?
            perror("There was an error with the command\n!");
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
	        			perror("There was an error opening or creating the out file.\n");
	        		} else {
	        			dup2(fd1,1);
	        		}
	        	}


	        	if(strlen(thisCommand[1].infile) > 0) {

	        		fd2 = getFileD(thisCommand[1].infile, paths, numPaths, false);
	        		if(fd2 == -1) {
	        			perror("There was an error opening the input file.\n");
	        			// TODO: make sure jobis not in jobs list
	        			continue;
	        		} else {
	        			dup2(fd2,0);
	        		}
	        	}

                char* cmdArray[thisCommand[1].numCmds+1];
        	    for (int i=0; i<thisCommand[1].numCmds; i++) {
        		    cmdArray[i] = thisCommand[1].cmd[i];
        	    }
                cmdArray[thisCommand[1].numCmds] = NULL;
                execvpe(thisCommand[1].cmd[0], cmdArray, environ);
                perror("There was an error with the command!\n");
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

            int checkpid2 = waitpid(ret3, &status, WUNTRACED);
            //printf("Waiting on PID: %d check pid: %d\n", ret3, checkpid2);
            while(1) {
               //printf("In loop\n");
                if(checkpid2 == ret3) {
                    if(WIFEXITED(status)) {
                        //printf("Exited, exit status %d\n", WEXITSTATUS(status));
                        printJob(jobs,ret3,true); 
                        break;
                    } else if (WIFSTOPPED(status)) {
                        break;
                    } else if (WIFSIGNALED(status)) {
                        //printf("Signal is: %d\n", WTERMSIG(status));
                        if(WTERMSIG(status) == SIGINT) {
                            printJob(jobs,ret3,true);   
                            break;
                        }   
                    }
                } 
                checkpid2 = waitpid(ret3, &status, WUNTRACED);
            }

            if(WIFEXITED(status)) {
		 		//set current job ID?
            } 

            currentHasPipe = false;   

        } else {
            updatePID(jobs, ret3);
            continue;
        }
        free(printBuf);	
        free(buf2);
        free(buf);
        free(currpath);
        
    }
  
}

void pushJob(struct Job* head, char* thisCmd, bool isRun, bool isRecent, int size, int pid)
{

    struct Job* current = head;

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

char* collectInput(struct Job* jobs) 
{
    int position = 0;
    int bufsize = MAX_BUFFER;
    char* buffer = malloc(sizeof(char) * bufsize);
    int c;
  
    if (!buffer) {
  	    fprintf(stderr, "Collect input failed to allocate buffer\n");
  	    exit(EXIT_FAILURE);
    }

    while(1) {  	
  	    c = getchar();

  	    if (c == EOF)
  	    {
            // iterate through all jobs and kill
            stopAllJobs(jobs);
            kill(getpid(), SIGKILL);
  	    } else if (c == '\n' || position == (200-1)) {
  	    	if(c != '\n') {
  	    		while(1) {
  	    			c = getchar();
  	    			if(c == '\n') {
  	    				return NULL;
  	    			}
  	    		} 

  	    	} else 
  	    	{
  		        buffer[position] = '\0';
  		        return buffer;
  		    }    	
     	} else {
  	    	buffer[position] = c;
  		    position++;
  	    } 
    }
    return buffer;
}

struct Command* parseInput(char* buf, char* buf2, bool* haspipe)
{
   
	int position = 0;
	for(int i=0; i < 200; i++) {
		if(buf[i] == '|' && !*haspipe) {

			buf[i] = '\0';
			*haspipe = true;
			i++;
			buf[i] = '\0';
		} else if(buf[i] == '|' && *haspipe) {
			return NULL;
		} else if (*haspipe) {
			buf2[position] = buf[i];
			position++;
			buf[i] = '\0';
		}
	}

	buf = trimTrailingWhitespace(buf);
	buf2 = trimTrailingWhitespace(buf2);

	struct Command retCmd = createCommand(buf);

	// Restrict pipe and bg
	if(retCmd.isForeground == false && *haspipe) return NULL;
	if(retCmd.cmd == NULL)  return NULL;

	struct Command* cmdList;
	struct Command retcmd2;

	if(*haspipe) {
        cmdList = malloc(2 * sizeof(struct Command));
        cmdList[1] = retCmd;
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

void handleSignal(int signal) {
    const char* signal_name;
    sigset_t pending;

    switch(signal) {
  	    case SIGINT:
  	        signal_name = "SIGINT";
  	        kill(currentChildPID, SIGINT);
  	        //currentChildPID = -1;
  	        break;
  	    case SIGTSTP:
  	        if(!currentHasPipe) {
  	            if(lastRemovedJobId == jobsSize-1) { 
  	                pushJob(jobs, currentCmd, false,true,lastRemovedJobId,currentChildPID);
  	                lastRemovedJobId = -1;
  	            } else {
  	        	    pushJob(jobs, currentCmd, false,true,jobsSize,currentChildPID);
  	        	    jobsSize++;	
  	            }
  	            kill(currentChildPID, SIGSTOP);
  	        } else {
  	        	printf("Can't background a command with a pipe.\n");
  	        }
  	        break;
  	    case SIGCHLD:
  	        break;
  	    default:
  	        printf("Can't find signal.\n");
  	        return;
    }
}

char* trimTrailingWhitespace(char* buf) {
	char *end;

	end = buf + strlen(buf) - 1;
	while(end > buf && *end == ' ') end--;
	*(end+1) = 0;

	return buf;
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
    // Find number of strings in this array
	for(int i=0; i<strlen(buf); i++) {
		if(buf[i] == ' ' || buf[i] == '\0') 
		{
			numCmds++;
		}
	}
    // Get all the strings divided by spaces
	char** cmds = malloc(sizeof(char*) * numCmds);
	char* token = strtok(buf, " ");
	int position = 0;
	while(token) {
		//printf("%s\n", token);
		cmds[position] =token;
		position++;
		token = strtok(NULL, " ");
	}

    // Figure if this command will be a background process
	char* infile = "";
	char* outfile = "";
	for(int i=0; i < numCmds; i++) {
		if((strcmp(cmds[i],"&") == 0) && isFor) {
            isFor = false;
            numCmds--;
            if(i<numCmds) {
            	struct Command thisCommand = {false, -1, false, true, NULL, -1, NULL, NULL};
            	return thisCommand;
            }
            break;
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

void printJobs(struct Job* jobsList) {

    struct Job* current = jobsList;
    while(current != NULL) {
    	if(current->cmd != NULL)
    	{
    	    printf("[%d]", current->id);
    	    if(current->isMostRecent) 
    	    	printf(" +");
    	    else
    	    	printf(" -");
    	    if(current->isRunning) 
    	    	printf(" Running ");
    	    else
    	    	printf(" Stopped ");
    	    printf(" %s\n", current->cmd);
    	    //printf(" PID: %d\n", current->pid);
    	} 
    	current = current->nextJob;   
    }
}

void printJob(struct Job* jobsList, int pid, bool jobDone) {

    struct Job* current = jobsList;
    while(current != NULL) {
    	if(current->cmd != NULL)
    	{
    	    if(current->pid == pid) {
    	    	printf("[%d]", current->id);
    	        if(current->isMostRecent) 
    	    	    printf(" +");
    	        else
    	    	    printf(" -");
    	        if(current->isRunning) 
    	    	    printf(" Running ");
    	        else if (jobDone)
    	        	printf(" Done ");
    	        else
    	    	    printf(" Stopped ");
    	        printf(" %s\n", current->cmd);
    	        //printf(" PID: %d\n", current->pid);
    	        return;
    	    }    
    	} 
    	current = current->nextJob;   
    }
}

void printDoneJob(char* cmd,int jobId) {
	printf("\n[%d] + Done  %s\n", jobId, cmd);
}
int foreground() 
{
    struct Job* current = jobs;
    while(current != NULL) {
    	if(current->cmd != NULL) 
    	{
    	    if(current->isMostRecent) 
    	    {

    	    	currentCmd = current->cmd;
    	    	printf("%s\n", currentCmd);
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

int background() 
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
    	        	printJob(jobs,currentChildPID,false);
    	    	    kill(current->pid, SIGCONT);
    	    	    return current->pid;
    	    	}    
    	    }               
    	} 
    	current = current->nextJob;   
    }
    return -1;
}


void stopAllJobs(struct Job* jobsList) {

	//printf("Stopping ALL JOBS\n");

    struct Job* current = jobsList;
    while(current != NULL) {
    	if(current->cmd != NULL)
    	{
    		//printf("Killing pid: %d\n", current->pid);
            kill(current->pid, SIGKILL);
    	} 
    	current = current->nextJob;   
    }
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

