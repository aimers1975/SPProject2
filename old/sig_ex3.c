/* file sig_ex3.c: This is a more complex example of signals
   Author: Ramesh Yerraballi
   Attempt to mimic:
     prompt>> top | grep firefox 
   The parent creates  a pipe and two child processes with the
   write end of the pipe serving as the stdout for top and
   the read end serving as the stdin for grep.
   The first child that exec's top creates a new session with itself a member leader
   of the process group in it. The process group's id is same as the child's pid (pid_ch1).
   The second child that exec's grep joins the process group that the first child created
   Now when a Ctr-c is pressed the parent relays a SIGINT to both children using 
      kill(-pid_ch1,SIGINT); alternative you could call killpg(pid_ch1,SIGINT); 
   The two child processes receive the SIGINT and their default behavior is to terminate.
   Once they do that the parent reaps their exit status (using wait), prints and exits.
   When a  Ctrl-z is pressed the the parent relays a SIGTSTP to both children using 
      kill(-pid_ch1,SIGTSTP);
   The parent's waitpid() call unblocks when the child receives the STOP signal. The parent
   waits for 4 secs and resumes the the child that STOPped. This happens once for each of 
   the two children.
*/
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

int pipefd[2];
int status, pid_ch1, pid_ch2, pid;

static void sig_int(int signo) {
  printf("Sending signals to group:%d\n",pid_ch1); // group id is pid of first in pipeline
  kill(-pid_ch1,SIGINT);
}
static void sig_tstp(int signo) {
  printf("Sending SIGTSTP to group:%d\n",pid_ch1); // group id is pid of first in pipeline
  kill(-pid_ch1,SIGTSTP);
}

int main(void) {

  char ch[1]={0};
  
  if (pipe(pipefd) == -1) {
    perror("pipe");
    exit(-1);
  }

  pid_ch1 = fork();
  if (pid_ch1 > 0){
    printf("Child1 pid = %d\n",pid_ch1);
    // Parent
    pid_ch2 = fork();
    if (pid_ch2 > 0){
      printf("Child2 pid = %d\n",pid_ch2);
      if (signal(SIGINT, sig_int) == SIG_ERR)
	         printf("signal(SIGINT) error");
      if (signal(SIGTSTP, sig_tstp) == SIG_ERR)
	       printf("signal(SIGTSTP) error");
      close(pipefd[0]); //close the pipe in the parent
      close(pipefd[1]);
      int count = 0;
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
	
        	if (WIFEXITED(status)) {
        	  printf("child %d exited, status=%d\n", pid, WEXITSTATUS(status));count++;
        	} else if (WIFSIGNALED(status)) {
        	  printf("child %d killed by signal %d\n", pid, WTERMSIG(status));count++;
        	} else if (WIFSTOPPED(status)) {
        	  printf("%d stopped by signal %d\n", pid,WSTOPSIG(status));
        	  printf("Sending CONT to %d\n", pid);
        	  sleep(4); //sleep for 4 seconds before sending CONT
        	  kill(pid,SIGCONT);
        	} else if (WIFCONTINUED(status)) {
        	  printf("Continuing %d\n",pid);
        	}
      }
      exit(1);
    }else {
      //Child 2
      sleep(1);
      int result = setpgid(0,pid_ch1); //child2 joins the group whose group id is same as child1's pid
      fprintf(stderr, "Set pgid: %d\n", result);
      close(pipefd[1]); // close the write end
      dup2(pipefd[0],STDIN_FILENO);
      char *myargs[3];
      myargs[0] = strdup("grep");   // program: "grep" (word count)
      myargs[1] = strdup("firefox");   // argument: "firefox"
      myargs[2] = NULL;           // marks end of array
      execvp(myargs[0], myargs);  // runs word count
    }
  } else {
      // Child 1
    int result = setsid(); // child 1 creates a new session and a new group and becomes leader -
              //   group id is same as his pid: pid_ch1
    fprintf(stderr, "Set sid result: %d\n", result);
    close(pipefd[0]); // close the read end
    dup2(pipefd[1],STDOUT_FILENO);
    char *myargs[2];
    myargs[0] = strdup("top");   // program: "top" (writes to stdout which is now pipe)
    myargs[1] = NULL;           
    execvp(myargs[0], myargs);  // runs top
  }
}
