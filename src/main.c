#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>

// Builtin commands for autocompletion
static const char *builtin_commands[] = {
  "echo",
  "exit",
  NULL
};

// Function to handle tab completion
void handle_tab_completion(char *buffer, int *pos) {
  // Only complete if we're at the beginning (first word)
  int i;
  for (i = 0; i < *pos; i++) {
    if (buffer[i] == ' ') {
      return; // Not the first word, don't complete
    }
  }
  
  // Find matching builtin command
  int len = *pos;
  const char *match = NULL;
  int match_count = 0;
  
  for (i = 0; builtin_commands[i] != NULL; i++) {
    if (strncmp(buffer, builtin_commands[i], len) == 0) {
      match = builtin_commands[i];
      match_count++;
    }
  }
  
  // If exactly one match, complete it
  if (match_count == 1) {
    int match_len = strlen(match);
    // Copy the rest of the match
    for (i = len; i < match_len; i++) {
      buffer[i] = match[i];
      printf("%c", match[i]);
      fflush(stdout);
    }
    // Add trailing space
    buffer[match_len] = ' ';
    printf(" ");
    fflush(stdout);
    *pos = match_len + 1;
  }
}

// Read input with tab completion support
char* read_input_with_completion(const char *prompt) {
  static char buffer[1024];
  int pos = 0;
  
  printf("%s", prompt);
  fflush(stdout);
  
  // Set terminal to raw mode for character-by-character input
  struct termios old_term, new_term;
  tcgetattr(STDIN_FILENO, &old_term);
  new_term = old_term;
  new_term.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
  
  while (1) {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) {
      // EOF or error
      tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
      return NULL;
    }
    
    if (c == '\t') {
      // Tab key - handle completion
      handle_tab_completion(buffer, &pos);
    } else if (c == '\n') {
      // Enter key
      buffer[pos] = '\0';
      printf("\n");
      fflush(stdout);
      break;
    } else if (c == 127 || c == 8) {
      // Backspace
      if (pos > 0) {
        pos--;
        printf("\b \b");
        fflush(stdout);
      }
    } else if (c == 4) {
      // Ctrl+D (EOF)
      if (pos == 0) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
        return NULL;
      }
    } else if (c >= 32 && c < 127) {
      // Printable character
      if (pos < 1023) {
        buffer[pos++] = c;
        printf("%c", c);
        fflush(stdout);
      }
    }
  }
  
  // Restore terminal mode
  tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
  
  return buffer;
}

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
// Modifies arg_count to remove redirection operators and filenames from args
// Sets stdout_file and stderr_file pointers to the redirect filenames (or NULL)
// Sets stdout_append and stderr_append to indicate if output should be appended
void parse_redirection(char **args, int *arg_count, char **stdout_file, char **stderr_file,
                       int *stdout_append, int *stderr_append) {
  *stdout_file = NULL;
  *stderr_file = NULL;
  *stdout_append = 0;
  *stderr_append = 0;
  
  int i = 0;
  while (i < *arg_count) {
    int is_redirect = 0;
    char **target_file = NULL;
    int *target_append = NULL;
    
    // Check for stdout append redirection (>> or 1>>)
    if (strcmp(args[i], ">>") == 0 || strcmp(args[i], "1>>") == 0) {
      is_redirect = 1;
      target_file = stdout_file;
      target_append = stdout_append;
      *target_append = 1;
    }
    // Check for stdout redirection (> or 1>)
    else if (strcmp(args[i], ">") == 0 || strcmp(args[i], "1>") == 0) {
      is_redirect = 1;
      target_file = stdout_file;
      target_append = stdout_append;
      *target_append = 0;
    }
    // Check for stderr append redirection (2>>)
    else if (strcmp(args[i], "2>>") == 0) {
      is_redirect = 1;
      target_file = stderr_file;
      target_append = stderr_append;
      *target_append = 1;
    }
    // Check for stderr redirection (2>)
    else if (strcmp(args[i], "2>") == 0) {
      is_redirect = 1;
      target_file = stderr_file;
      target_append = stderr_append;
      *target_append = 0;
    }
    
    if (is_redirect && i + 1 < *arg_count) {
      // Next argument should be the filename
      *target_file = args[i + 1];
      // Remove redirection operator and filename from args
      // Shift remaining args
      for (int j = i; j < *arg_count - 2; j++) {
        args[j] = args[j + 2];
      }
      *arg_count -= 2;
      args[*arg_count] = NULL;
      // Don't increment i, check the same position again
    } else {
      i++;
    }
  }
}

int main(int argc, char *argv[]) {
  // Suppress unused parameter warnings
  (void)argc;
  (void)argv;
  
  // Flush after every printf
  setbuf(stdout, NULL);
  
//REPL Loop
while(1){
  // Read User's Input with tab completion support
  char *input = read_input_with_completion("$ ");
  
  // Check for EOF (Ctrl+D)
  if(input == NULL) {
    break;
  }
  
  // Copy to a separate buffer for safety
  char command[1024];
  strncpy(command, input, sizeof(command) - 1);
  command[sizeof(command) - 1] = '\0';

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

  // Check for output and error redirection
  char *stdout_file = NULL;
  char *stderr_file = NULL;
  int stdout_append = 0;
  int stderr_append = 0;
  parse_redirection(args, &arg_count, &stdout_file, &stderr_file, &stdout_append, &stderr_append);
  
  int stdout_fd = -1;
  int stderr_fd = -1;

  // Open files for redirection if specified
  if (stdout_file != NULL) {
    int flags = O_WRONLY | O_CREAT | (stdout_append ? O_APPEND : O_TRUNC);
    stdout_fd = open(stdout_file, flags, 0644);
    if (stdout_fd < 0) {
      perror("open");
      continue;
    }
  }
  
  if (stderr_file != NULL) {
    int flags = O_WRONLY | O_CREAT | (stderr_append ? O_APPEND : O_TRUNC);
    stderr_fd = open(stderr_file, flags, 0644);
    if (stderr_fd < 0) {
      perror("open");
      if (stdout_fd >= 0) close(stdout_fd);
      continue;
    }
  }

  //Check for echo command
  if(strcmp(args[0], "echo")==0){
    // Handle redirection for echo
    int saved_stdout = -1;
    int saved_stderr = -1;
    
    if (stdout_fd >= 0) {
      saved_stdout = dup(STDOUT_FILENO);
      dup2(stdout_fd, STDOUT_FILENO);
      close(stdout_fd);
    }
    
    if (stderr_fd >= 0) {
      saved_stderr = dup(STDERR_FILENO);
      dup2(stderr_fd, STDERR_FILENO);
      close(stderr_fd);
    }
    
    for(int i = 1; i < arg_count; i++){
      if(i > 1) printf(" ");
      printf("%s", args[i]);
    }
    printf("\n");
    
    // Restore stdout and stderr if redirected
    if (saved_stdout >= 0) {
      dup2(saved_stdout, STDOUT_FILENO);
      close(saved_stdout);
    }
    if (saved_stderr >= 0) {
      dup2(saved_stderr, STDERR_FILENO);
      close(saved_stderr);
    }
    continue;
  }

  //Check for pwd command
  if(strcmp(args[0], "pwd")==0){
    // Handle redirection for pwd
    int saved_stdout = -1;
    int saved_stderr = -1;
    
    if (stdout_fd >= 0) {
      saved_stdout = dup(STDOUT_FILENO);
      dup2(stdout_fd, STDOUT_FILENO);
      close(stdout_fd);
    }
    
    if (stderr_fd >= 0) {
      saved_stderr = dup(STDERR_FILENO);
      dup2(stderr_fd, STDERR_FILENO);
      close(stderr_fd);
    }
    
    char cwd[1024];
    if(getcwd(cwd, sizeof(cwd)) != NULL){
      printf("%s\n", cwd);
    } else {
      perror("pwd");
    }
    
    // Restore stdout and stderr if redirected
    if (saved_stdout >= 0) {
      dup2(saved_stdout, STDOUT_FILENO);
      close(saved_stdout);
    }
    if (saved_stderr >= 0) {
      dup2(saved_stderr, STDERR_FILENO);
      close(saved_stderr);
    }
    continue;
  }

  //Check for cd command
  if(strcmp(args[0], "cd")==0){
    // cd produces error output, so handle stderr redirection
    int saved_stderr = -1;
    
    if (stderr_fd >= 0) {
      saved_stderr = dup(STDERR_FILENO);
      dup2(stderr_fd, STDERR_FILENO);
      close(stderr_fd);
    }
    
    // Close stdout fd if opened (cd doesn't use stdout)
    if (stdout_fd >= 0) {
      close(stdout_fd);
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
    
    // Restore stderr if redirected
    if (saved_stderr >= 0) {
      dup2(saved_stderr, STDERR_FILENO);
      close(saved_stderr);
    }
    continue;
  }

  //Check for type command
  if(strcmp(args[0], "type")==0){
    // Handle redirection for type
    int saved_stdout = -1;
    int saved_stderr = -1;
    
    if (stdout_fd >= 0) {
      saved_stdout = dup(STDOUT_FILENO);
      dup2(stdout_fd, STDOUT_FILENO);
      close(stdout_fd);
    }
    
    if (stderr_fd >= 0) {
      saved_stderr = dup(STDERR_FILENO);
      dup2(stderr_fd, STDERR_FILENO);
      close(stderr_fd);
    }
    
    //Get the argument after "type"
    if(arg_count < 2){
      // Restore stdout and stderr if redirected
      if (saved_stdout >= 0) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
      }
      if (saved_stderr >= 0) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
      }
      continue;
    }
    char *arg = args[1];

    //Check if the argument is a built-in command
    if(strcmp(arg, "echo") ==0 || strcmp(arg, "exit") ==0 || strcmp(arg, "type")==0 || strcmp(arg, "pwd")==0 || strcmp(arg, "cd")==0){
      printf("%s is a shell builtin\n", arg);
      // Restore stdout and stderr if redirected
      if (saved_stdout >= 0) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
      }
      if (saved_stderr >= 0) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
      }
      continue;  // IMPORTANT: continue here to skip PATH search
    }

    // Search for executable in PATH (only if not a builtin)
    char *path_env = getenv("PATH");
    if(path_env == NULL) {
      printf("%s: not found\n", arg);
      // Restore stdout and stderr if redirected
      if (saved_stdout >= 0) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
      }
      if (saved_stderr >= 0) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
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
    
    // Restore stdout and stderr if redirected
    if (saved_stdout >= 0) {
      dup2(saved_stdout, STDOUT_FILENO);
      close(saved_stdout);
    }
    if (saved_stderr >= 0) {
      dup2(saved_stderr, STDERR_FILENO);
      close(saved_stderr);
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
      // Handle output and error redirection
      if (stdout_fd >= 0) {
        dup2(stdout_fd, STDOUT_FILENO);
        close(stdout_fd);
      }
      if (stderr_fd >= 0) {
        dup2(stderr_fd, STDERR_FILENO);
        close(stderr_fd);
      }
      execv(full_path, args);
      // If execv returns, there was an error
      perror("execv failed");
      exit(1);
    } else if(pid > 0){
      // Parent process
      if (stdout_fd >= 0) {
        close(stdout_fd);
      }
      if (stderr_fd >= 0) {
        close(stderr_fd);
      }
      wait(NULL);
    } else {
      perror("fork");
      if (stdout_fd >= 0) {
        close(stdout_fd);
      }
      if (stderr_fd >= 0) {
        close(stderr_fd);
      }
    }
  } else {
    printf("%s: command not found\n", args[0]);
    if (stdout_fd >= 0) {
      close(stdout_fd);
    }
    if (stderr_fd >= 0) {
      close(stderr_fd);
    }
  }
}  // End of main while loop

  return 0;
}