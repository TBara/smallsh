#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#define BUFFER_MAX 2048
#define ARG_MAX 513

void catchSIGINT(int signo)
{
  char* message = "SIGINT. Use CTRL-Z to Stop.\n";
  write(STDOUT_FILENO, message, 28);
}

void parseInput(char *input, char *arg_arr[], int *arg_cnt, char* input_file, char* outputfile, int *bg);

void main (){
    char *arg_arr[ARG_MAX]; // Store passed in arguments
    int arg_cnt = 0;

    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = catchSIGINT;
    sigfillset(&SIGINT_action.sa_mask);
    //SIGINT_action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &SIGINT_action, NULL);

    int numCharsEntered = -5; // How many chars we entered
    int currChar = -5; // Tracks where we are when we print out every char
    size_t buffer_size = 0; // Holds how large the allocated buffer is
    char* userInput = NULL; // Points to a buffer allocated by getline() that holds our entered string + \n + \0
    char* input_file = NULL; // Points to input file from args
	char* output_file = NULL; // POints to output file from args
    int background = 0;

    while(1)
    {
    // Get input from the user
    while(1)
    {
        printf("Enter command:");
        // Get a line from the user
        numCharsEntered = getline(&userInput, &buffer_size, stdin);
        if (numCharsEntered == -1)
        clearerr(stdin);
        else
        break; // Exit the loop - we've got input
    }
    // Remove the trailing \n that getline adds
    userInput[strcspn(userInput, "\n")] = '\0';
    parseInput(userInput, arg_arr, &arg_cnt, input_file, output_file, &background); // Tokenizes the input and stores it in arg_arr

    int i;
    printf("Args: %d\n", arg_cnt);
    for (i = 0; i < arg_cnt; i++){
        printf("%s\n", arg_arr[i]);
    }

    // Free the memory allocated by getline() or else memory leak
    free(userInput);
    userInput = NULL;
    }

}

// Receives user's input and parses into arguments, sets file redirects, and sets background run flag
void parseInput(char *input, char *arg_arr[], int *arg_cnt, char* input_file, char* output_file, int *bg){
    
    // Reset the arguments array and count
    memset(arg_arr, '\0', ARG_MAX);
    (*arg_cnt) = 0;
    // Get teh first argumant
    char* token = strtok(input, " ");
    char* expand;
    
    while(token != NULL){
        if(strncmp(token, "#", 1) == 0){
            return;
        } else if (strcmp(token, ">") == 0){
            token =(strtok(NULL, " \n"));
            output_file = strdup(token);

        } else if (strcmp(token, "<") ==0){
			token=(strtok(NULL, " \n"));
			input_file = strdup(token);

        } else if (strcmp(token, "&") ==0){
            *bg = 1; // Set the background-run flag
        } else if(strstr(token, "$$")){
            // Found $$ in a string. Expand it to pid
            int shell_pid = getpid(); // Get teh pid
            char sh_pid[10];
            sprintf(sh_pid, "%d", shell_pid); // Convert pid to char
            
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
            token = strtok(NULL, " ");
            (*arg_cnt)++;

            // TODO: what about multiple occurance of $$ ? 

        } else {
            // Store in the array, 
            arg_arr[*arg_cnt] = strdup(token);
            token = strtok(NULL, " ");
            (*arg_cnt)++;
        }
        token = strtok(NULL, " \n");
    }
}