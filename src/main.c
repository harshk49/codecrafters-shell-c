#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  printf("$ ");

  // Read User's Input
  
  char command[1024];
  fgets(command, sizeof(command), stdin);

  // Remove newline character from input
  command[strcspn(command, "\n")] = "\0";

  //Print error Message
  printf("%s: command not found\n", command);

  return 0;
}
