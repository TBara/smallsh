#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/types.h>

#define BUFFER_MAX 2048
#define ARG_MAX 513

struct Child_Exit_Info {
    pid_t pid;
    int   status;
};

const char *AMP = "&";
volatile sig_atomic_t B_GND = 0; 
volatile sig_atomic_t PID_CNT = 0;
int STATUS = 0;
struct Child_Exit_Info PID_ARR[ARG_MAX];

// Prototypes
void readStatus();
void add_pid_arr(struct Child_Exit_Info);
void chk_pid_arr();
void sigint_handler(int signum);
void sigchld_handler(int signum);
void sigtstp_handler(int signum);
void parseInput(char *input, char *arg_arr[], int *arg_cnt, char *input_file, char *outputfile);
bool isBackground(char *arg_arr[], int *arg_cnt);
void chng_dir(char *arg_arr[]);
void process_cmd(char *arg_arr[], int *arg_cnt, int *status);
void getStatus(pid_t *child_status);
void start_foreground(char *arg_arr[], char *input_file, char *outputfile, int *status);
void start_background(char *arg_arr[], char *input_file, char *outputfile);
void set_input_fd_bg(int *sourceFD, int *result, char *input_file);
void set_output_fd_bg(int *targetFD, int *result, char *output_file);
void set_input_fd_fg(int *sourceFD, int *result, char *input_file);
void set_output_fd_fg(int *targetFD, int *result, char *output_file);
void getInput(char *userInput, size_t buffer_size);

// Main driver function
void main(){
    char *arg_arr[ARG_MAX]; // Store passed in arguments
    int arg_cnt = 0;

    // Catches and handles signals from completed child processes
    struct sigaction SIGCHLD_action = {0};
    SIGCHLD_action.sa_handler = sigchld_handler;
    sigfillset(&SIGCHLD_action.sa_mask);
    SIGCHLD_action.sa_flags=SA_RESTART;
    sigaction(SIGCHLD, &SIGCHLD_action, NULL);
    
    // Catches and handles SIGINT - ^C
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = sigint_handler;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags=SA_RESTART;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Catches and handles SIGTSTP - ^Z
    struct sigaction sig_tstp={0};	
	sig_tstp.sa_handler = &sigtstp_handler;
	sigfillset(&sig_tstp.sa_mask);
	sig_tstp.sa_flags=SA_RESTART;
	sigaction(SIGTSTP, &sig_tstp, NULL); 

    int numCharsEntered = -5;
    int *status;
    status = &STATUS;
    size_t buffer_size = 0;   
    char *userInput = NULL;  

    while (1)
    {
        char *input_file = malloc( sizeof(char) * BUFFER_MAX);;  // Points to input file from args
        char *output_file = malloc( sizeof(char) * BUFFER_MAX); // Points to output file from args

        // Get input from the user
        while (1)
        {
            //fflush(stdout);
            printf(": ");
            //write(STDOUT_FILENO, ": ", 2);
            fflush(stdout);
            numCharsEntered = getline(&userInput, &buffer_size, stdin); // Get a line from the user
            if (numCharsEntered == -1) {
                clearerr(stdin);
            } else {
                break; // Exit the loop - we've got input
            }
        }

        // Remove the trailing \n that getline adds
        userInput[strcspn(userInput, "\n")] = '\0';
        parseInput(userInput, arg_arr, &arg_cnt, input_file, output_file); // Tokenizes the input and stores it in arg_arr

        if ((arg_cnt > 0) && (strcmp(arg_arr[0], "cd") == 0)){
            chng_dir(arg_arr);  /// Navigate dirs

        } else if ((arg_cnt > 0) && (strcmp(arg_arr[0], "exit") == 0)) {
            exit(EXIT_SUCCESS); // Exit program

        } else if ((arg_cnt > 0) && (strcmp(arg_arr[0], "status") == 0)) {
            getStatus(status); // Status of last foreground process

        } else {
            // Check the globla variable if foreground-only is enabled
            if(B_GND == 0){
                // Run in either mode
                if ((arg_cnt > 0) && isBackground(arg_arr, &arg_cnt)){
                    //printf("run in background\n"); fflush(stdout);
                    start_background(arg_arr, input_file, output_file);
                } else if ((arg_cnt > 0) && !isBackground(arg_arr, &arg_cnt)) {
                    //printf("run in foreground\n"); fflush(stdout);
                    start_foreground(arg_arr, input_file, output_file, status);
                }

            } else if(B_GND == 1){
                // Run in foreground no matter what
                isBackground(arg_arr, &arg_cnt);
                start_foreground(arg_arr, input_file, output_file, status);            
            }            
        }

        // Free the memory allocated by getline() or else memory leak
        free(input_file);
        free(output_file);
        free(userInput);
        userInput = NULL;
    }
}

// Add a struct, with pid and exit status, to an array
void add_pid_arr(struct Child_Exit_Info chld){
    //printf("Adding pid: %d\n", chld.pid);
    if(PID_CNT < (ARG_MAX - 1)){
        PID_ARR[PID_CNT] = chld;
        PID_CNT++;
    }
}

// Iterate PID_ARR and write out status of completed child prcesses
void chk_pid_arr(){
    while (PID_CNT > 0)
    {
        PID_CNT--;
        int stat = PID_ARR[PID_CNT].status;
        pid_t *status = &stat;
        printf("background pid %d is done: ", PID_ARR[PID_CNT].pid);
        getStatus(status);
        // Reset read values
        PID_ARR[PID_CNT].pid = -5;
        PID_ARR[PID_CNT].status = -5;
        
    }
}

// Run processes in the foreground, parant waits for child to finish
void start_foreground(char *arg_arr[], char *input_file, char *output_file, int *status){
    //printf("Run in foreground\n");
    int sourceFD, targetFD, result;
    pid_t spawnPid = -5;
    pid_t childPid = -5;
    int childExitStatus = -5;
    spawnPid = fork(); // Fork new process
    
    switch (spawnPid)
    {
        case -1: { 
            // Some error happened
            perror("Something went wrong when forking\n"); 
            exit(2); 
            break;
            }
        case 0: {
            // Do any input/output redirection
            set_input_fd_fg(&sourceFD, &result, input_file);
            set_output_fd_fg(&targetFD, &result, output_file);

            // Execute the command with params
            execvp(*arg_arr, arg_arr);
            
            // This should not execute
            perror("");
            exit(1);
            break;
        }
        default:
        //Parent; Check if any process has completed, block parent until child process terminates
        childPid = waitpid(spawnPid, &childExitStatus, 0);
        *status = childExitStatus;
        break;
    }
}

// Run processes in the background, parant does not wait for child to finish
void start_background(char *arg_arr[], char *input_file, char *output_file){
    int sourceFD, targetFD, result;
    pid_t spawnPid = -5;
    pid_t childPid = -5;
    int childExitStatus = -5;
    spawnPid = fork();
    switch (spawnPid)
    {
    case -1:
    {
        perror("Hull Breach!\n");
        exit(1);
        break;
    }
    case 0:
    {
        printf("background pid is %i\n", getpid());
        fflush(stdout);
        // Do any file redirects
        set_input_fd_bg(&sourceFD, &result, input_file);
        set_output_fd_bg(&targetFD, &result, output_file);

        // Execute the command with params
        execvp(*arg_arr, arg_arr);
        perror("CHILD: exec failure!\n");
        exit(2);
        break;
    }
    default:
        {
            // Don't wait for child to finish
            childPid = waitpid(spawnPid, &childExitStatus,  WNOHANG);
            break;
        }
    }
    // Close files
    close(sourceFD);
    close(targetFD);
}

// Set input file descriptor for background processes
void set_input_fd_bg(int *sourceFD, int *result, char *input_file){
    // If no file path supplied, send to black hole
    if (strlen(input_file) < 1)
    {
        strcpy(input_file, "/dev/null");
    }
    
    // If file path is supplied, open
    *sourceFD = open(input_file, O_RDONLY);
    if (*sourceFD == -1) { 
        perror(""); 
        exit(1); 
    }

    // set input, check for error
    *result = dup2(*sourceFD, 0);
    if (*result == -1) { 
        perror(""); 
        exit(2); 
    }
}

// Set output file descriptor for background processes
void set_output_fd_bg(int *targetFD, int *result, char *output_file){
    // If no file path supplied, send to black hole
    if (strlen(output_file) < 1)
    {
        strcpy(output_file, "/dev/null");
    }
    
    // If file path is supplied, open or create it, in overwrite mode, set permissions
    *targetFD = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (*targetFD == -1) { 
        perror(""); 
        exit(1); 
    }

    // set output, check for error
    *result = dup2(*targetFD, 1);
    if (*result == -1) { 
        perror(""); 
        exit(2); 
    }
}

// Set input file descriptor for foreground processes
void set_input_fd_fg(int *sourceFD, int *result, char *input_file){
    if (strlen(input_file) > 0)
    {
        // If file path is supplied, open
        *sourceFD = open(input_file, O_RDONLY);
        if (*sourceFD == -1) { 
            perror("cannot open badfile for input"); 
            exit(1); 
        }

        // Set input, check for error
        *result = dup2(*sourceFD, 0);
        if (*result == -1) { 
            perror("source dup2()"); 
            exit(2); 
        }
    }
}

// Set output file descriptor for foreground processes
void set_output_fd_fg(int *targetFD, int *result, char *output_file){
    if (strlen(output_file) > 0)
    {
        // If file path is supplied, open or create it, in overwrite mode, set permissions
        *targetFD = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (*targetFD == -1) { 
            perror("target open()"); 
            exit(1); 
        }

        // set output, check for error
        *result = dup2(*targetFD, 1);
        if (*result == -1) { 
            perror("target dup2()"); 
            exit(2); 
        }
        
    }
    

}

// Checks the status of process that's finished
void getStatus(pid_t *child_status){
    int tmp = *child_status;
    //printf("tmp %d\n", tmp);
    fflush(stdout);
    if (WIFEXITED(*child_status) != 0)
    {
        // get status after normal exit
        printf("exit value %d\n", (int)WEXITSTATUS(*child_status));
        fflush(stdout);
        STATUS = tmp;
    }
    else if (WIFSIGNALED(*child_status) != 0)
    {
        // get exit signal
        printf("terminated by signal %d\n", (int)WTERMSIG(*child_status));
        fflush(stdout);
    }
}

// Changes cwd to home or to supplied path
void chng_dir(char *arg_arr[]){
    char *dir;
    if (arg_arr[1] == NULL)
    {
        // No path supplied, get path to home from env vars
        dir = getenv("HOME");
        if (chdir(dir) != 0)
        {
            perror("cd command failed\n");
        }
    }
    else if ((arg_arr[1] != NULL) && (arg_arr[2] == NULL))
    {
        // move to specified path
        dir = arg_arr[1];
        if (chdir(dir) != 0)
        {
            perror("cd command failed.\n");
        }
    }
}

// Returns true if the last argument is &
bool isBackground(char *arg_arr[], int *arg_cnt){
    if (strcmp(arg_arr[(*arg_cnt) - 1], AMP) == 0)
    {
        // Remove the & from arg_arr
        arg_arr[(*arg_cnt) - 1] = NULL;
        (*arg_cnt)--;
        return true;
    }
    else
    {
        return false;
    }
}

// Receives user's input and parses into arguments, sets file redirects, and sets background run flag
void parseInput(char *input, char *arg_arr[], int *arg_cnt, char *input_file, char *output_file){
    memset(input_file, '\0', BUFFER_MAX);
    memset(output_file, '\0', BUFFER_MAX);
    memset(arg_arr, '\0', ARG_MAX);
    (*arg_cnt) = 0;
    // Get teh first argumant
    char *token = strtok(input, " ");
    char *expand;

    while (token != NULL)
    {
        if (strncmp(token, "#", 1) == 0)
        {
            // Ignore
            return;
        }
        else if (strcmp(token, ">") == 0)
        {
            // next token is a file path
            token = (strtok(NULL, " "));
            strcpy(output_file, token);
        }
        else if (strcmp(token, "<") == 0)
        {
            // next token is a file path
            token = (strtok(NULL, " "));
            strcpy(input_file, token);
        }
        else if (strstr(token, "$$"))
        {
            // Found $$ in a string. Expand it to pid
            // Get teh pid
            // Convert pid to char
            int shell_pid = getpid();
            char sh_pid[10];
            sprintf(sh_pid, "%d", shell_pid);

            // Get the chars after $$
            char sufix[BUFFER_MAX];
            memset(sufix, '\0', BUFFER_MAX);
            strcpy(sufix, token);
            expand = strstr(sufix, "$$");

            //  Get chars before $$
            char prefix[BUFFER_MAX];
            memset(prefix, '\0', BUFFER_MAX);
            strncpy(prefix, token, strlen(token) - strlen(expand));

            // Concatenate the prifix + pid + sufix
            sprintf(token, "%s%s%s", prefix, sh_pid, expand + 2);
            // Store in the array
            arg_arr[*arg_cnt] = strdup(token);
            (*arg_cnt)++;
            token = (strtok(NULL, " "));
        }
        else
        {
            // Store in the array,
            arg_arr[*arg_cnt] = strdup(token);
            (*arg_cnt)++;
        }
        // Increment token
        token = strtok(NULL, " ");
    }
}

// Prints out the terminating signal
void readStatus(){
    printf("terminated by signal %d\n", STATUS);
    fflush(stdout);
}

// Handle SIGINT signal
void sigint_handler(int signum){
    STATUS = signum;
    readStatus();
}

// Handle SIGCHLD signal
void sigchld_handler(int signum){
    pid_t pid;
    int   status;
    pid = waitpid(-1, &status, WNOHANG); // Get the pid
    
    if (pid > 1)
    {
        struct Child_Exit_Info chld_info = {pid, status};
        add_pid_arr(chld_info);
    }
    chk_pid_arr();
}

// Toggle background-only mode on/off
void sigtstp_handler(int signum){
    if (B_GND == 0)
    {
        B_GND = 1;
        char* message = "\nEntering foreground-only mode (& is now ignored)\n";
	    write(STDOUT_FILENO, message, 50);
    } else {

        char* message = "\nExiting foreground-only mode\n";
	    write(STDOUT_FILENO, message, 30);
        B_GND = 0;
    }
}