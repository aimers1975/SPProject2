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

int pipefd[2];
int status, pid;
int pid_ch1 = -1;
int pid_ch2 = -1;
int jobsSize = 1;
struct Job* jobs;
bool currentHasPipe;
char* currentCmd;
int lastRemovedJobId = -1;
int currentChildPID = -1;

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
static void sig_int(int);
static void sig_tstp(int);


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

void runInputLoop(char* buf) {

    //char ch[1]={0};
    pid_ch1 = -1;
    pid_ch2 = -1;


    while(1) {;
        int fd1, fd2;
        printf("****** Starting new loop!*******\n");

        buf = malloc(sizeof(char) * MAX_BUFFER);
        char* buf2 = malloc(sizeof(char) * MAX_BUFFER);
        char* printBuf = NULL; 
        printBuf = malloc(sizeof(char) * MAX_BUFFER);
        bool haspipe = false;

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
          printf("bring job to background\n");
          background();
          continue;
        } else if(strcmp(buf, "fg") == 0) {
          foreground();
          printf("Bring job to foreground\n");
          continue;
        }

        if(!thisCommand->isForeground) {
          pushJob(jobs,printBuf,true,true,jobsSize,jobsSize);
          jobsSize++;
        } else {
          currentCmd = printBuf;
        }    

        if(haspipe) {
          if (pipe(pipefd) == -1) {
              perror("pipe");
              exit(-1);
          }
        }  

        pid_ch1 = fork();
      
        if (pid_ch1 > 0) {

             // Parent
            if(haspipe) {
                pid_ch2 = fork();
            }
            if (pid_ch2 > 0) {

                printf("Parent says Child2 pid = %d\n",pid_ch2);
                if (signal(SIGINT, sig_int) == SIG_ERR)
          	        printf("signal(SIGINT) error");
                if (signal(SIGTSTP, sig_tstp) == SIG_ERR)
          	        printf("signal(SIGTSTP) error");
                
                if(haspipe) {
                    close(pipefd[0]); //close the pipe in the parent
                    close(pipefd[1]);
                }



            } else if (pid_ch2 == 0){
                  //Child 2

                  if(haspipe) {
                      printf("Child2 thisCommand[1] cmd: %s\nChild1 thisCommand[0] cmd: %s\n", thisCommand[1].cmd[0], thisCommand[0].cmd[0]);
                      setpgid(0,pid_ch1); //child2 joins the group whose group id is same as child1's pid
                      dup2(pipefd[1],STDOUT_FILENO);
                      close(pipefd[0]);
                      currentHasPipe = true;

                  } else if(strlen(thisCommand[1].outfile) > 0) {
                      fd1 = getFileD(thisCommand[1].outfile, paths, numPaths, true);
                      if(fd1 == -1)
                      {
                          printf("There was an error opening or creating the out file.\n");
                      } else {
                          dup2(fd1,1);
                      } 
                  }


                  if(strlen(thisCommand[1].infile) > 0) {
                      fd2 = getFileD(thisCommand[1].infile, paths, numPaths, false);
                      if(fd2 == -1) {
                          printf("There was an error opening the input file.\n");
                          // TODO: make sure jobis not in jobs list
                          continue;
                      } else {
                          dup2(fd2,0);
                      }
                  } 

                  

                  //printf("READER calling exec: %s\n", thisCommand[0].cmd[0]);
                  char* cmdArray[thisCommand[1].numCmds+1];
                  for (int i=0; i<thisCommand[1].numCmds; i++) {
                      cmdArray[i] = thisCommand[1].cmd[i];
                  }
                      cmdArray[thisCommand[1].numCmds] = NULL;
                  printf("CALLING CHILD 2 EXEC (READER)!!!! : %s\n", thisCommand[1].cmd[0]);
                  execvpe(thisCommand[1].cmd[0], cmdArray, environ);
                    //TODO: do we ever execut here?
                  perror("There was an error with the command\n!");
                  if (fd1 > 0) close(fd1);
                  if (fd2 > 0) close(fd2);

                  
                  printf("Child2 exiting\n");
                  exit(1);
            }

        } else {
            // Child 1


            if(haspipe) {
                setsid(); // child 1 creates a new session and a new group and becomes leader -
                      //   group id is same as his pid: pid_
                currentHasPipe = true;
                dup2(pipefd[0],STDIN_FILENO);
                close(pipefd[1]);

            } else if(strlen(thisCommand[0].infile) > 0) {

                fd2 = getFileD(thisCommand[0].infile, paths, numPaths, false);
                if(fd2 == -1) {
                    perror("There was an error opening the input file.\n");
                    // TODO: make sure jobis not in jobs list
                    continue;
                } else {
                    dup2(fd2,STDIN_FILENO);
                }
            }

            if(strlen(thisCommand[0].outfile) > 0) {

                fd1 = getFileD(thisCommand[0].outfile, paths, numPaths, true);
                if(fd1 == -1)
                {
                    perror("There was an error opening or creating the out file.\n");
                } else {
                  dup2(fd1,STDOUT_FILENO);
                }
            }

            perror("CALLING CHILD 1 EXEC!!!\n");
            char* cmdArray[thisCommand[0].numCmds+1];
            for (int i=0; i<thisCommand[0].numCmds; i++) {
                cmdArray[i] = thisCommand[0].cmd[i];
            }
            cmdArray[thisCommand[0].numCmds] = NULL;
            execvpe(thisCommand[0].cmd[0], cmdArray, environ);
            perror("There was an error with the command!\n");
            if (fd1 > 0) close(fd1);
            if (fd2 > 0) close(fd2);


            perror("Child 1 exiting\n");
            exit(0);
        }
        
        if(thisCommand[0].isForeground) {
                int count;
            if(haspipe) {
                count = 0;
            } else {
                count = 1;
            }

            while (count < 2) {
                // Parent's wait processing is based on the sig_ex4.c
                pid = waitpid(-1, &status, WUNTRACED | WCONTINUED);
                // wait does not take options:
                //    waitpid(-1,&status,0) is same as wait(&status)
                // with no options waitpid wait only for terminated child processes
                // with options we can specify what other changes in the child's status
                // we can respond to. Here we are saying we want to also know if the child
                // has been stopped (WUNTRACED) or continued (WCONTINUED)

                if (pid == -1) {
                  perror("waitpid");
                  exit(EXIT_FAILURE);
                }
                printf("----------------------------Count is now %d\n", count);
                if (WIFEXITED(status)) {
                  printf("child %d exited, status=%d\n", pid, WEXITSTATUS(status));count++;
                } else if (WIFSIGNALED(status)) {
                  printf("child %d killed by signal %d\n", pid, WTERMSIG(status));count++;
                } else if (WIFSTOPPED(status)) {
                  printf("%d stopped by signal %d\n", pid,WSTOPSIG(status));
                  printf("Sending CONT to %d\n", pid);
                  //kill(pid,SIGCONT);
                } else if (WIFCONTINUED(status)) {
                  printf("Continuing %d\n",pid);
                }
            }
        } else {
            updatePID(jobs, pid_ch1);
            continue;
        }
        perror("BOTH CHILDREN DONE!!!\n");
        //exit(1);
    }
}


static void sig_int(int signo) {
    printf("Sending signals to group:%d\n",pid_ch1); // group id is pid of first in pipeline
    kill(-pid_ch1,SIGINT);
}
static void sig_tstp(int signo) {
    printf("Sending SIGTSTP to group:%d\n",pid_ch1); // group id is pid of first in pipeline
    kill(-pid_ch1,SIGTSTP);
}

void handleSignal(int signal) {

    const char* signal_name;

    switch(signal) {
        case SIGINT:
            signal_name = "SIGINT";
            // Send signal to most foreground running process

            printf("Signal is: %s", signal_name);
            sig_int(SIGINT);
            break;
        case SIGTSTP:
            signal_name = "SIGTSTP";
            printf("Signal is: %s", signal_name);

            if(!currentHasPipe) {
                if(lastRemovedJobId == jobsSize-1) { 
                    pushJob(jobs, currentCmd, false,true,lastRemovedJobId,currentChildPID);
                    lastRemovedJobId = -1;
                } else {
                  pushJob(jobs, currentCmd, false,true,jobsSize,currentChildPID);
                  jobsSize++; 
                }
                sig_int(SIGSTOP);
            } else {
              printf("Can't background a command with a pipe.\n");
            }
           
            break;
        case SIGCHLD:
            printf("New child created!\n");
            break;
        default:
            printf("Can't find signal.\n");

            return;
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
    printf("RUNNING GET FILD\n");
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
