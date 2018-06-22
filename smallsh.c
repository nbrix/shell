/*******************************************************************************
* Programmer: 	Nikolas Brix						       *							 
* Date: 	3/5/2018						       *							 
* Description: 	A shell that has three built-in commands: cd, status, and exit.*
*		It supports I/O redirection and both background and foreground *
*		processes.   						       *					   *
*******************************************************************************/

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

//Constants
#define BUFFER_SIZE 2048	//max number of characters read f command line
#define MAX_NUMBER_ARG 512	//max number of arguments per command
#define MAX_CHILD_PROCESSES 32	//max number of background processes that can 
				//be run simultaneously
//Global Variables
struct sigaction SIGINT_action = {0};		//struct for SIGINT
struct sigaction SIGTSTP_action = {0};		//struct for SIGTSTP
int TSTP_Flag = 0;	//specifies if the shell accepts background processes		
					
/*******************************************************************************
*				FUNCTION PROTOTYPES			       *
*******************************************************************************/

void KillChildren(pid_t *, size_t);
int ChangeDir(char *);
char *ExpandPID(char *);
char **ParseInput(char *); 
int RedirectIO(char **argv, int );
int IsBGProcess(char **argv);
int RemovePID(pid_t, pid_t *, size_t *);
int PushPID(pid_t, pid_t *, size_t *);
void Execute(char **argv, pid_t *, size_t *, int *);
void DisplayStatus(int);
void ExecuteCommand(char **argv, pid_t *, size_t *, int *);

/*******************************************************************************
*        			SIGNAL HANDLERS			     	       *
*******************************************************************************/

/*******************************************************************************
* Description: Catches SIGINT and prints out a message that the process was    *
*	       terminated.						       *
*******************************************************************************/
void catchSIGINT(int signo)
{            
	char* message = "terminated by signal 2\n";
	write(STDOUT_FILENO, message, 23);   
}

/*******************************************************************************
* Description: Catches SIGTSTP and switches the mode 'foreground-only' ON/OFF. *
*	       When entering foreground-only, all new background processes are *
*	       ignored and run through the foreground.			       *
*******************************************************************************/
void catchSIGTSTP(int signo)
{
	if (!TSTP_Flag) {	//flag that determines which mode the shell is in
		char* message = "\nEntering foreground-only mode (& is now ignored)\n";
		write(STDOUT_FILENO, message, 50);
		TSTP_Flag = 1;		//reset flag
	} else {
		char* message = "\nExiting foreground-only mode\n";
		write(STDOUT_FILENO, message, 30);
		TSTP_Flag = 0;		//reset flag
	}
}

/*******************************************************************************
*        			FUNCTIONS	   			       *		     
*******************************************************************************/

/*******************************************************************************
* Description: Iterates through all background processes and sends a signal to *
*	       terminate them.		   		   		       *
* Input: Array of child processes and its size.				       *								
* Output: None					     			       *							 
*******************************************************************************/
void KillChildren(pid_t *bg_list, size_t size)
{
	for (size_t i = 0; i < size; i++)
		kill(bg_list[i], SIGKILL);
}

/*******************************************************************************
* Description: Changes the working directory.			      	       *
* Input: Desired file path to change directory to of type char *	       *																								 
* Output: Integer that determines if the function was successful.	       *
*******************************************************************************/
int ChangeDir(char *file_path)
{
	char *home_dir = getenv("HOME");	//stores the environment variable HOME

	if (file_path == NULL) {	//if no argument was given, go to home directory
		if(chdir(home_dir) != 0) {			
			perror("Error");
			return 1;
		}
	} else { 	//check if directory exists and changing directory is successful
		if (chdir(file_path) != 0) {	
			perror("Error");
			return 1;
		}
	}

	return 0;
}

/*******************************************************************************
* Description: Expands $$ into the process id.			   	       *
* Input: String of data to be parsed of type char *			       *																								 
* Output: char * of input parameter with $$ expanded.			       *
*******************************************************************************/
char *ExpandPID(char *line)
{
	char buffer[BUFFER_SIZE];
	char pid[16];
	char *ptr = line;
	
	sprintf(pid, "%d", getpid());               //convert pid into string
	
    while(ptr = strstr(ptr, "$$")) {
	//copy line into buffer up until first occurrence of $$
        strncpy(buffer, line, ptr - line); 
        buffer[ptr - line] = '\0'; //NULL terminate string to allow concat
        strcat(buffer, pid);                    
        strcat(buffer, ptr + 2);
        strcpy(line, buffer);
    }

	return line;
}

/*******************************************************************************
* Description: Parses the input string into an array of arguments.	       *
* Input: String of data to be parsed of type char *			       *																								 
* Output: Array of pointers of type char.				       *
*******************************************************************************/
char **ParseInput(char *line)
{
	int index = 0;			
	char *tok;		//token string
	char **argv = malloc(MAX_NUMBER_ARG * sizeof(char *)); //argument array

	line = ExpandPID(line);			//expand any instance of $$ in the string
	tok = strtok(line, " ");		//get first argument from input

	while(tok && index < MAX_NUMBER_ARG) {	//copy token into arguments array
		argv[index] = tok;											 				
		tok = strtok(NULL, " ");
		index++;
	}

	argv[index] = NULL;		//ensure the last entry into the array is NULL
	return argv;			//return size of arguments array
}

/*******************************************************************************
* Description: Redirects stdin and stdout to a specified file given by the     *
*	       user. Parses through an array of pointers looking for  	       *
*	       '<' or '>', then uses the arguments passed through to redirect  *
*	       to/from the file.					       *
* Input: Array of pointers containing the command and arguments specified by   *
*	 the user. 							       *																								 
* Output: Returns an integer of 0 if successful.			       *
*******************************************************************************/
int RedirectIO(char **argv, int isBG)
{
	int i = 0;
	int src_file, tgt_file;		//source and target file descriptors

	while (argv[i] != NULL){
		if (!strcmp(argv[i], ">")) {		//output redirection
			//open file specified by user to redirect output to
			if (argv[i + 1] == NULL && isBG)						
				tgt_file = open("/dev/null", O_WRONLY);	
			else
				tgt_file = open(argv[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
			
			argv[1] = NULL;	//ensures only the command is passed through to child

			//redirect
			if(dup2(tgt_file, STDOUT_FILENO) < 0) {	
				perror("target dup2()");
				exit(1);
			}
			fcntl(tgt_file, F_SETFD, FD_CLOEXEC);	//close file on exec()
		}
		else if (!strcmp(argv[i], "<")) {	//input redirection
			if (argv[i + 1] == NULL && isBG)	//open file specified by user
				src_file = open("/dev/null", O_RDONLY);
			else
				src_file = open(argv[i + 1], O_RDONLY);

			argv[1] = NULL;

			//redirect
			if(dup2(src_file, STDIN_FILENO) < 0) {	
				perror("source dup2()");
				exit(1);
			}
			fcntl(src_file, F_SETFD, FD_CLOEXEC);
		}
		i++;
	}
	return 0;
}

/*******************************************************************************
* Description: Checks if a process should be ran in the background. Determines *
*	       this by checking if the user entered a '&' at the end of their  *
*	       command.                                                        *
* Input:  Array of pointers consisting of arguments passed by user.            *																								 
* Output: Returns an integer of 1 if the process should run in the background, *
*	  otherwise it returns 0.					       *
*******************************************************************************/
int IsBGProcess(char **argv)
{
	int i = 0;
	while (argv[i] != NULL) {		//finds size of argument list
		i++;
	}
	if (!strcmp(argv[i - 1], "&")) {	//checks for special character  
		argv[i - 1] = NULL;		//deletes special character

		if(TSTP_Flag)			//checks if foreground-only mode is on
			return 0;
		else
			return 1;
		
	} else {
		return 0;
	}
}

/*******************************************************************************
* Description: Removes process from list of active background processes.       *
* Input: Process id, list of background processes and its size.		       *																								 
* Output: Returns an integer of 0 if the process was successfully removed.     *
*	  Otherwise it returns -1.					       *
*******************************************************************************/
int RemovePID(pid_t pid, pid_t *bg_list, size_t *size)
{
	//iterate through array searching for specified process to remove
	for (size_t i = 0; i < (*size); i++) {
		if (bg_list[i] == pid) {
			if (i == MAX_CHILD_PROCESSES - 1) {
				bg_list[i] = 0;
			} else {	//restructure array so there are no gaps between pids
				for (int j = i; j < (*size); j++)
					bg_list[j] = bg_list[j + 1];
			}
			(*size)--;
			return 0;
		}
	}
	return -1;
}

/*******************************************************************************
* Description: Pushes PID of active background process into an array.	       *
* Input: Process id, list of background processes and its size.		       *																								 
* Output: Returns an integer of 0 if successful. Otherwise, returns -1 if      *
*	  there are too many background processes running.		       *
*******************************************************************************/
int PushPID(pid_t pid, pid_t *bg_list, size_t *size)
{
	if (*size >= MAX_CHILD_PROCESSES) {	//check if too many processes running
		printf("Too many child processes running\n");
		return -1;
	} else {
		bg_list[*size] = pid;		//add process to array
		(*size)++;
		return 0;
	}
}

/*******************************************************************************
* Description: Creates child process to execute non-built-in commands. Also,   *
*	       stores any active background process into an array. Then checks * 
*	       and removes process if it finishes.			       *
* Input: Array of pointers consisting of arguments, array of background	       *
*	 processes currently running, its size, and status of last foreground  *
*	 process that completed.					       *																								 
* Output: None 	  							       *
*******************************************************************************/
void Execute(char **argv, pid_t *bg_list, size_t *size, int *last_process)
{
	pid_t pid = -5;	
	pid_t cpid;
	int status = -5;		//status of process
	int isBG;			//flag: should process run in background

	isBG = IsBGProcess(argv);	//set flag
	pid = fork();
	switch (pid) {
		//ERROR
		case -1:
			perror("Error forking");
			exit(1);
			break;
		
		//CHILD PROCESS
		case 0:	
			//reset SIGINT to default, if process is not in bg
			if (!isBG) {	
				SIGINT_action.sa_handler = SIG_DFL;
				sigaction(SIGINT, &SIGINT_action, NULL);      	
			}

			RedirectIO(argv, isBG);		//check for any redirection

			if (execvp(argv[0],argv) == -1) {
				perror("Error with execvp()");
				exit(1);
			}
			break;
		
		//PARENT PROCESS
		default:	
			//push pid to array if running in background
			if (isBG) {		
				PushPID(pid, bg_list, size);		
				printf("background pid is %d\n", pid);
				fflush(stdout);
			} else {
				//make sure waitpid() returns if interrupted
				do {	
					cpid = waitpid(pid, &status, 0);
				} while(errno == EINTR && cpid == -1);
				*last_process = status; 
			}
			//check if any child process running in the background has completed
			cpid = waitpid(-1, &status, WNOHANG);
			if (cpid > 0) {
				if(!RemovePID(cpid, bg_list, size)) {
					printf("backround pid %d is done: ", cpid);
					fflush(stdout);
					DisplayStatus(status);		//get status of how process ended
				}
			}
			break;
	}
}

/*******************************************************************************
* Description: Displays the status of the last process that ended.   	       *
* Input: Int variable consisting of the status of the last process that        *
*	 completed.						   	       *																								 
* Output: None 	  							       *
*******************************************************************************/
void DisplayStatus(int status)
{
	pid_t cpid;
	int exit_status;

	if (WIFEXITED(status)) {		//process exited normally
		exit_status = WEXITSTATUS(status); 
		printf("exit value %d\n", exit_status);
	} else if (WIFSIGNALED(status)) {	//process was exited by a signal
		exit_status = WTERMSIG(status);
		printf("terminated by signal %d\n", exit_status);
	}
	fflush(stdout);
}

/*******************************************************************************
* Description: Checks and executes the command the user entered. Supports three*
*	       built-in functions: exit - exits shell and kills all processes; *
*	       cd - changes working directory; and status - displays how the   *
*	       process ended.				       		       *
* Input: Array of pointers containing the arguments the user specified.	       *																								 
* Output: None 	  						    	       *
*******************************************************************************/
void ExecuteCommand(char **argv, pid_t *bg_list, size_t *size, int *last_process)
{
	//check if command is a comment
	if (argv[0][0] == '#') {
		return;
	} else if (!strcmp(argv[0],"exit")) {	//exits shell
		KillChildren(bg_list, *size);
		free(argv);
		exit(0);
	} else if (!strcmp(argv[0],"cd")) {	//changes directory
		if (argv[1] != NULL)
			ChangeDir(argv[1]);	//pass through only one argument
		else
			ChangeDir(NULL);	//if no argument was given pass in NULL
	} else if (!strcmp(argv[0],"status")) {
		DisplayStatus(*last_process);
	} else {				//execute non-built-in commands
		Execute(argv, bg_list, size, last_process);
	}
}

/*******************************************************************************
*				MAIN					       *
*******************************************************************************/
int main()
{
	char line[BUFFER_SIZE];		//user input
    	char **argv;
	int last_process;		//stores the last foreground process that ran
	size_t size = 0;			//size of bg_list array
	pid_t bg_list[MAX_CHILD_PROCESSES];	//array that stores all background 
						//processes currently running
	//handle SIGTSTP
	SIGTSTP_action.sa_handler = catchSIGTSTP;           
    	sigfillset(&SIGTSTP_action.sa_mask);
    	SIGTSTP_action.sa_flags = 0; 
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	//handle SIGINT
    	SIGINT_action.sa_handler = catchSIGINT;           
    	sigfillset(&SIGINT_action.sa_mask);
    	SIGINT_action.sa_flags = 0; 
	sigaction(SIGINT, &SIGINT_action, NULL);

	while(1) {	//run shell
	    
		printf(": ");
		fflush(stdout);

		// get user input
	    	char *user_input = fgets(line, BUFFER_SIZE, stdin);
	    	line[strlen(line)-1] = 0;

	    	//if input is blank, reprompt user
	    	if (strlen(line) == 0)
	    		continue;
		
	    	if (user_input != NULL) {	//if a line was read, continue executing
	    		argv = ParseInput(line);
			ExecuteCommand(argv, bg_list, &size, &last_process);
			free(argv);
	    	}			
	}
	
	return 0;
}
