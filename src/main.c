#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
      printf("%s is a shell built-in command\n", arg);
    } else {
      printf("%s: not found\n", arg);
    }
      continue;
    }
  

  //Print error Message
  printf("%s: command not found\n", command);
}

  return 0;
}
