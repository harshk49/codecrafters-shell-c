#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>

// Parse command line with single and double quote support
int parse_command(char *input, char **args, int max_args) {
  int arg_count = 0;
  int i = 0;
  int len = strlen(input);
  
  // Buffer to build current argument
  static char arg_buffer[64][1024];  // Static storage for parsed arguments
  int buf_idx = 0;
  
  while (i < len && arg_count < max_args - 1 && arg_count < 64) {
    // Skip leading whitespace
    while (i < len && isspace(input[i])) {
      i++;
    }
    
    if (i >= len) break;
    
    // Start building a new argument
    int arg_len = 0;
    char quote_char = 0;  // 0 = not in quotes, '\'' = single quote, '"' = double quote
    
    // Parse the argument (may contain multiple quoted/unquoted segments)
    while (i < len) {
      // Handle backslash escaping outside quotes
      if (input[i] == '\\' && quote_char == 0 && i + 1 < len) {
        // Skip the backslash and take the next character literally
        i++;
        arg_buffer[buf_idx][arg_len++] = input[i];
        i++;
        continue;
      }
      
      if ((input[i] == '\'' || input[i] == '"') && quote_char == 0) {
        // Start of quoted section
        quote_char = input[i];
        i++;
        continue;
      } else if (input[i] == quote_char && quote_char != 0) {
        // End of quoted section
        quote_char = 0;
        i++;
        continue;
      } else if (quote_char == 0 && isspace(input[i])) {
        // End of argument (unquoted whitespace)
        break;
      } else {
        // Regular character (inside or outside quotes)
        arg_buffer[buf_idx][arg_len++] = input[i];
        i++;
      }
    }
    
    // Null-terminate and save the argument
    if (arg_len > 0 || quote_char != 0) {  // Allow empty quotes
      arg_buffer[buf_idx][arg_len] = '\0';
      args[arg_count++] = arg_buffer[buf_idx];
      buf_idx++;
    }
  }
  
  args[arg_count] = NULL;
  return arg_count;
}

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);
//REPL Loop
while(1){
  printf("$ ");

  // Read User's Input
  
  char command[1024];
  if(fgets(command, sizeof(command), stdin)==NULL)
  {
    break; // Exit on EOF
  }

  // Remove newline character from input
  command[strcspn(command, "\n")] = '\0';

  //Check for exit command
  if(strcmp(command, "exit")==0){
    break;
  }

  //Parse command and arguments with quote support
  char *args[64];
  int arg_count = parse_command(command, args, 64);

  if(arg_count ==0){
    continue; // No command entered
  }

  //Check for echo command
  if(strcmp(args[0], "echo")==0){
    for(int i = 1; i < arg_count; i++){
      if(i > 1) printf(" ");
      printf("%s", args[i]);
    }
    printf("\n");
    continue;
  }

  //Check for pwd command
  if(strcmp(args[0], "pwd")==0){
    char cwd[1024];
    if(getcwd(cwd, sizeof(cwd)) != NULL){
      printf("%s\n", cwd);
    } else {
      perror("pwd");
    }
    continue;
  }

  //Check for cd command
  if(strcmp(args[0], "cd")==0){
    //Get the argument after "cd"
    char *path = arg_count > 1 ? args[1] : "~";
    
    // Handle ~ for home directory
    if(strcmp(path, "~") == 0){
      char *home = getenv("HOME");
      if(home != NULL){
        path = home;
      }
    }
    
    //Try to change directory
    if(chdir(path) != 0){
      printf("cd: %s: No such file or directory\n", path);
    }
    continue;
  }

  //Check for type command
  if(strcmp(args[0], "type")==0){
    //Get the argument after "type"
    if(arg_count < 2){
      continue;
    }
    char *arg = args[1];

    //Check if the argument is a built-in command
    if(strcmp(arg, "echo") ==0 || strcmp(arg, "exit") ==0 || strcmp(arg, "type")==0 || strcmp(arg, "pwd")==0 || strcmp(arg, "cd")==0){
      printf("%s is a shell builtin\n", arg);
      continue;  // IMPORTANT: continue here to skip PATH search
    }

    // Search for executable in PATH (only if not a builtin)
    char *path_env = getenv("PATH");
    if(path_env == NULL) {
      printf("%s: not found\n", arg);
      continue;
    }

    // Make a copy of PATH to tokenize
    char path_copy[4096];
    strncpy(path_copy, path_env, sizeof(path_copy)-1);
    path_copy[sizeof(path_copy)-1] = '\0';

    char *dir = strtok(path_copy, ":");
    int found = 0;

    while(dir != NULL){
      // Build full path to the command
      char full_path[2048];
      snprintf(full_path, sizeof(full_path), "%s/%s", dir, arg);

      //Check if the file exists and is executable
      if(access(full_path, X_OK) == 0){
        printf("%s is %s\n", arg, full_path);
        found = 1;
        break;
      }

      dir = strtok(NULL, ":");
    }

    if(!found){
      printf("%s: not found\n", arg);
    }
    continue;  // Continue after handling type command
  }

  // Try to execute as external command
  char *path_env = getenv("PATH");
  if(path_env ==NULL){
    printf("%s: command not found\n", args[0]);
    continue; 
  }

  // Make a copy of PATH to tokenize
  char path_copy[4096];
  strncpy(path_copy, path_env, sizeof(path_copy)-1);
  path_copy[sizeof(path_copy)-1] = '\0';

  char *dir = strtok(path_copy, ":");
  int found = 0;
  char full_path[2048];

  while(dir != NULL){
    // Build full path to the command
    snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[0]);

    if(access(full_path, X_OK)==0){
      found = 1;
      break;
    }
    dir = strtok(NULL, ":");
  }

  if(found){
    pid_t pid = fork();
    if(pid == 0){
      // Child process
      execv(full_path, args);
      // If execv returns, there was an error
      perror("execv failed");
      exit(1);
    } else if(pid > 0){
      // Parent process
      wait(NULL);
    } else {
      perror("fork");
    }
  } else {
    printf("%s: command not found\n", args[0]);
  }
}  // End of main while loop

  return 0;
}