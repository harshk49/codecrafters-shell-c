#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>

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
      
      // Handle backslash escaping inside double quotes
      if (input[i] == '\\' && quote_char == '"' && i + 1 < len) {
        char next_char = input[i + 1];
        // Within double quotes, backslash only escapes: ", \, $, `, and newline
        // For this stage, we handle: double-quote and backslash
        if (next_char == '"' || next_char == '\\') {
          // Skip the backslash and take the escaped character literally
          i++;
          arg_buffer[buf_idx][arg_len++] = input[i];
          i++;
          continue;
        }
        // For other characters, treat backslash literally
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

// Check for output redirection and parse it
// Returns the filename for redirection, or NULL if no redirection found
// Modifies arg_count to remove redirection operator and filename from args
char* parse_redirection(char **args, int *arg_count) {
  for (int i = 0; i < *arg_count; i++) {
    // Check for > or 1> operator
    if (strcmp(args[i], ">") == 0 || strcmp(args[i], "1>") == 0) {
      // Next argument should be the filename
      if (i + 1 < *arg_count) {
        char *filename = args[i + 1];
        // Remove redirection operator and filename from args
        // Shift remaining args (if any) and null-terminate
        for (int j = i; j < *arg_count - 2; j++) {
          args[j] = args[j + 2];
        }
        args[*arg_count - 2] = NULL;
        *arg_count -= 2;
        return filename;
      }
    }
  }
  return NULL;
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

  // Check for output redirection
  char *redirect_file = parse_redirection(args, &arg_count);
  int redirect_fd = -1;

  // If redirection is specified, open the file
  if (redirect_file != NULL) {
    redirect_fd = open(redirect_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (redirect_fd < 0) {
      perror("open");
      continue;
    }
  }

  //Check for echo command
  if(strcmp(args[0], "echo")==0){
    // Handle redirection for echo
    int saved_stdout = -1;
    if (redirect_fd >= 0) {
      saved_stdout = dup(STDOUT_FILENO);
      dup2(redirect_fd, STDOUT_FILENO);
      close(redirect_fd);
    }
    
    for(int i = 1; i < arg_count; i++){
      if(i > 1) printf(" ");
      printf("%s", args[i]);
    }
    printf("\n");
    
    // Restore stdout if redirected
    if (saved_stdout >= 0) {
      dup2(saved_stdout, STDOUT_FILENO);
      close(saved_stdout);
    }
    continue;
  }

  //Check for pwd command
  if(strcmp(args[0], "pwd")==0){
    // Handle redirection for pwd
    int saved_stdout = -1;
    if (redirect_fd >= 0) {
      saved_stdout = dup(STDOUT_FILENO);
      dup2(redirect_fd, STDOUT_FILENO);
      close(redirect_fd);
    }
    
    char cwd[1024];
    if(getcwd(cwd, sizeof(cwd)) != NULL){
      printf("%s\n", cwd);
    } else {
      perror("pwd");
    }
    
    // Restore stdout if redirected
    if (saved_stdout >= 0) {
      dup2(saved_stdout, STDOUT_FILENO);
      close(saved_stdout);
    }
    continue;
  }

  //Check for cd command
  if(strcmp(args[0], "cd")==0){
    // cd doesn't produce output, so no redirection needed but close fd
    if (redirect_fd >= 0) {
      close(redirect_fd);
    }
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
    // Handle redirection for type
    int saved_stdout = -1;
    if (redirect_fd >= 0) {
      saved_stdout = dup(STDOUT_FILENO);
      dup2(redirect_fd, STDOUT_FILENO);
      close(redirect_fd);
    }
    
    //Get the argument after "type"
    if(arg_count < 2){
      // Restore stdout if redirected
      if (saved_stdout >= 0) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
      }
      continue;
    }
    char *arg = args[1];

    //Check if the argument is a built-in command
    if(strcmp(arg, "echo") ==0 || strcmp(arg, "exit") ==0 || strcmp(arg, "type")==0 || strcmp(arg, "pwd")==0 || strcmp(arg, "cd")==0){
      printf("%s is a shell builtin\n", arg);
      // Restore stdout if redirected
      if (saved_stdout >= 0) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
      }
      continue;  // IMPORTANT: continue here to skip PATH search
    }

    // Search for executable in PATH (only if not a builtin)
    char *path_env = getenv("PATH");
    if(path_env == NULL) {
      printf("%s: not found\n", arg);
      // Restore stdout if redirected
      if (saved_stdout >= 0) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
      }
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
    
    // Restore stdout if redirected
    if (saved_stdout >= 0) {
      dup2(saved_stdout, STDOUT_FILENO);
      close(saved_stdout);
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
      // Handle output redirection
      if (redirect_fd >= 0) {
        dup2(redirect_fd, STDOUT_FILENO);
        close(redirect_fd);
      }
      execv(full_path, args);
      // If execv returns, there was an error
      perror("execv failed");
      exit(1);
    } else if(pid > 0){
      // Parent process
      if (redirect_fd >= 0) {
        close(redirect_fd);
      }
      wait(NULL);
    } else {
      perror("fork");
      if (redirect_fd >= 0) {
        close(redirect_fd);
      }
    }
  } else {
    printf("%s: command not found\n", args[0]);
    if (redirect_fd >= 0) {
      close(redirect_fd);
    }
  }
}  // End of main while loop

  return 0;
}