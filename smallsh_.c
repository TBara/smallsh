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
const char *AMP = "&";

void catchSIGINT(int signo);
void parseInput(char *input, char *arg_arr[], int *arg_cnt, char *input_file, char *outputfile);
bool isBackground(char *arg_arr[], int *arg_cnt);
void chng_dir(char *arg_arr[]);
void process_cmd(char *arg_arr[], int *arg_cnt, int *status);
void getStatus(int *child_status);
void start_foreground(char *arg_arr[], char *input_file, char *outputfile);
void start_background(char *arg_arr[], char *input_file, char *outputfile);

void main()
{
    char *arg_arr[ARG_MAX]; // Store passed in arguments
    int arg_cnt = 0;
    
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = catchSIGINT;
    sigfillset(&SIGINT_action.sa_mask);
    //SIGINT_action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &SIGINT_action, NULL);

    int numCharsEntered = -5; // How many chars we entered
    int status = 0;
    size_t buffer_size = 0;   // Holds how large the allocated buffer is
    char *userInput = NULL;   // Points to a buffer allocated by getline() that holds our entered string + \n + \0

    while (1)
    {
        char *input_file = malloc( sizeof(char) * BUFFER_MAX);;  // Points to input file from args
        char *output_file = malloc( sizeof(char) * BUFFER_MAX); // Points to output file from args

        // Get input from the user
        while (1)
        {
            printf("Enter command:");
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
            chng_dir(arg_arr);

        } else if ((arg_cnt > 0) && (strcmp(arg_arr[0], "exit") == 0)) {
            exit(EXIT_SUCCESS);

        } else if ((arg_cnt > 0) && (strcmp(arg_arr[0], "status") == 0)) {
            getStatus(&status);

        } else if ((arg_cnt > 0) && isBackground(arg_arr, &arg_cnt)){
            start_background(arg_arr, input_file, output_file);

        } else if ((arg_cnt > 0) && !isBackground(arg_arr, &arg_cnt)) {
            start_foreground(arg_arr, input_file, output_file);
        }

        /*************** Delete after testing ********/
        // int i;
        // printf("Args: %d\n", arg_cnt);
        // printf("In_f: %s\n", input_file);
        // printf("Out_f: %s\n", output_file);
        // for (i = 0; i < arg_cnt; i++){
        //     printf("Arg%d: %s\n", i, arg_arr[i]);
        // }
        /*********************************************/

        // Free the memory allocated by getline() or else memory leak
        free(input_file);
        free(output_file);
        free(userInput);
        userInput = NULL;
    }
}

void start_foreground(char *arg_arr[], char *input_file, char *outputfile){
    printf("Run in foreground\n");

    pid_t spawnPid = -5;
    pid_t actualPid = -5;
    int childExitStatus = -5;
    spawnPid = fork(); // Fork new process
    
    switch (spawnPid)
    {
        case -1: { 
            // Unknown error
            perror("Something went wrong when forking\n"); 
            exit(1); 
            break;
            }
        case 0: {
            // Execute the command with params
            execvp(*arg_arr, arg_arr);
            perror("Exec failure\n");
            exit(2);
            break;
        }
        default:
        // Check if any process has completed, block parent until child process terminates
        actualPid = waitpid(spawnPid, &childExitStatus, 0);
        break;
    }

}

//
void set_input_fd(int *sourceFD, int *result, char *input_file){
    if (strlen(input_file) < 1)
    {
        strcpy(input_file, "/dev/null");
    }
    
    *sourceFD = open(input_file, O_RDONLY);
    if (*sourceFD == -1) { 
        perror("source open()"); 
        exit(1); 
    }

    *result = dup2(*sourceFD, 0);
    if (*result == -1) { 
        perror("source dup2()"); 
        exit(2); 
    }
}

//
void set_output_fd(int *targetFD, int *result, char *output_file){
    if (strlen(output_file) < 1)
    {
        strcpy(output_file, "/dev/null");
    }
    
    *targetFD = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (*targetFD == -1) { 
        perror("target open()"); 
        exit(1); 
    }

    *result = dup2(*targetFD, 1);
    if (*result == -1) { 
        perror("target dup2()"); 
        exit(2); 
    }
}

void start_background(char *arg_arr[], char *input_file, char *output_file){
    printf("Run in background\n");

    int sourceFD, targetFD, result;
    pid_t spawnPid = -5;
    pid_t actualPid = -5;
    int childExitStatus = -5;
    spawnPid = fork(); // Fork new process
    
    switch (spawnPid)
    {
        case -1: { 
            // Some error happened
            perror("Something went wrong when forking\n"); 
            exit(1); 
            break;
            }
        case 0: {
            // 
            set_input_fd(&sourceFD, &result, input_file);
            set_output_fd(&targetFD, &result, output_file);

            // Execute the command with params
            execvp(*arg_arr, arg_arr);
            perror("Exec failure\n");
            exit(2);
            break;
        }
        default:
        // Check if the process specified has completed, return immediately with 0 if it hasn’t
        actualPid = waitpid(spawnPid, &childExitStatus, WNOHANG);
        break;
    }
    close(sourceFD);
}

void getStatus(int *child_status)
{
    if (WIFEXITED(child_status) != 0)
    {
        // get status after normal exit
        printf("exit value %d\n", (int)WEXITSTATUS(child_status));
        fflush(stdout);
    }
    else if (WIFSIGNALED(child_status) != 0)
    {
        // get exit signal
        printf("terminated by signal %d\n", (int)WTERMSIG(child_status));
        fflush(stdout);
    }
}

// Changes cwd to home or to supplied path
void chng_dir(char *arg_arr[])
{
    char *dir;
    if (arg_arr[1] == NULL)
    {
        // No path supplied
        dir = getenv("HOME");
        if (chdir(dir) != 0)
        {
            perror("cd command failed to execute.\n");
        }
    }
    else if ((arg_arr[1] != NULL) && (arg_arr[2] == NULL))
    {
        // move to specified path
        dir = arg_arr[1];
        if (chdir(dir) != 0)
        {
            perror("cd command failed to execute.\n");
        }
    }
    // else {
    //     // Do something??? Too many args
    // }

    char s[100]; // Delete
    printf("cwd: %s\n", getcwd(s, 100));
}

// Returns true if the last argument is &
bool isBackground(char *arg_arr[], int *arg_cnt)
{
    if (strcmp(arg_arr[(*arg_cnt) - 1], AMP) == 0)
    {
        //printf("& found\n");
        arg_arr[(*arg_cnt) - 1] = NULL;
        (*arg_cnt)--;
        return true;
    }
    else
    {
        //printf("& NOT found\n");
        return false;
    }
}

// Receives user's input and parses into arguments, sets file redirects, and sets background run flag
void parseInput(char *input, char *arg_arr[], int *arg_cnt, char *input_file, char *output_file)
{
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
            return;
        }
        else if (strcmp(token, ">") == 0)
        {
            token = (strtok(NULL, " "));
            strcpy(output_file, token);
        }
        else if (strcmp(token, "<") == 0)
        {
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
        token = strtok(NULL, " ");
    }
}

//
void catchSIGINT(int signo)
{
    char *message = "SIGINT detected. Exiting...\n";
    write(STDOUT_FILENO, message, 28);
}