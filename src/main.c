#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

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

  //Check for echo command
  if(strncmp(command, "echo ", 5)==0){
    //Print everything after "echo"
    printf("%s\n", command + 5);
    continue;
  }

  //Handle "echo" with no arguments
  if(strcmp(command, "echo")==0){
      printf("\n");
      continue;
  }

  //Check for type command
  if(strncmp(command, "type ", 5)==0){
    //Get the argument after "type"
    char *arg = command + 5;

    //Check if the argument is a built-in command
    if(strcmp(arg, "echo") ==0 || strcmp(arg, "exit") ==0 || strcmp(arg, "type")==0){
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

  // Print Error Message for unknown commands
  printf("%s: command not found\n", command);
}

  return 0;
}