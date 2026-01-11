#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <dirent.h>

// Builtin commands for autocompletion
static const char *builtin_commands[] = {
  "echo",
  "exit",
  NULL
};

// Compare function for qsort to sort strings alphabetically
int string_compare(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

// Calculate longest common prefix of all strings in the array
int find_longest_common_prefix(char **matches, int match_count, char *lcp, int max_len) {
  if (match_count == 0) {
    lcp[0] = '\0';
    return 0;
  }
  
  if (match_count == 1) {
    strncpy(lcp, matches[0], max_len - 1);
    lcp[max_len - 1] = '\0';
    return strlen(lcp);
  }
  
  // Start with the first string as the base
  int lcp_len = 0;
  int first_len = strlen(matches[0]);
  
  for (int i = 0; i < first_len && lcp_len < max_len - 1; i++) {
    char c = matches[0][i];
    
    // Check if all other strings have the same character at this position
    int all_match = 1;
    for (int j = 1; j < match_count; j++) {
      if (i >= strlen(matches[j]) || matches[j][i] != c) {
        all_match = 0;
        break;
      }
    }
    
    if (!all_match) {
      break;
    }
    
    lcp[lcp_len++] = c;
  }
  
  lcp[lcp_len] = '\0';
  return lcp_len;
}

// Function to handle tab completion
void handle_tab_completion(char *buffer, int *pos, int *tab_count, char *last_prefix, int *last_prefix_len) {
  // Only complete if we're at the beginning (first word)
  int i;
  for (i = 0; i < *pos; i++) {
    if (buffer[i] == ' ') {
      *tab_count = 0;  // Reset tab count if we're not on first word
      return; // Not the first word, don't complete
    }
  }
  
  int len = *pos;
  
  // Check if the prefix has changed since last tab press
  if (len != *last_prefix_len || strncmp(buffer, last_prefix, len) != 0) {
    *tab_count = 0;  // Reset tab count if prefix changed
  }
  
  // Increment tab count
  (*tab_count)++;
  
  // Save current prefix
  strncpy(last_prefix, buffer, len);
  last_prefix[len] = '\0';
  *last_prefix_len = len;
  
  char matches[1024][256];  // Store potential matches
  int match_count = 0;
  
  // Check builtin commands
  for (i = 0; builtin_commands[i] != NULL; i++) {
    if (strncmp(buffer, builtin_commands[i], len) == 0) {
      strncpy(matches[match_count], builtin_commands[i], 255);
      matches[match_count][255] = '\0';
      match_count++;
    }
  }
  
  // Check executables in PATH
  char *path_env = getenv("PATH");
  if (path_env != NULL) {
    char path_copy[4096];
    strncpy(path_copy, path_env, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    
    char *dir = strtok(path_copy, ":");
    while (dir != NULL && match_count < 1024) {
      DIR *d = opendir(dir);
      if (d != NULL) {
        struct dirent *entry;
        while ((entry = readdir(d)) != NULL && match_count < 1024) {
          // Check if the entry name starts with our prefix
          if (strncmp(entry->d_name, buffer, len) == 0) {
            // Build full path to check if it's executable
            char full_path[2048];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);
            
            // Check if it's executable
            if (access(full_path, X_OK) == 0) {
              // Check if we already have this match (avoid duplicates)
              int duplicate = 0;
              for (int j = 0; j < match_count; j++) {
                if (strcmp(matches[j], entry->d_name) == 0) {
                  duplicate = 1;
                  break;
                }
              }
              
              if (!duplicate) {
                strncpy(matches[match_count], entry->d_name, 255);
                matches[match_count][255] = '\0';
                match_count++;
              }
            }
          }
        }
        closedir(d);
      }
      dir = strtok(NULL, ":");
    }
  }
  
  // If no matches, ring the bell
  if (match_count == 0) {
    printf("\x07");
    fflush(stdout);
    *tab_count = 0;  // Reset tab count
    return;
  }
  
  // If exactly one match, complete it
  if (match_count == 1) {
    int match_len = strlen(matches[0]);
    // Copy the rest of the match
    for (i = len; i < match_len; i++) {
      buffer[i] = matches[0][i];
      printf("%c", matches[0][i]);
      fflush(stdout);
    }
    // Add trailing space
    buffer[match_len] = ' ';
    printf(" ");
    fflush(stdout);
    *pos = match_len + 1;
    *tab_count = 0;  // Reset tab count after completion
  }
  // If multiple matches, try to complete to longest common prefix
  else {
    // Find longest common prefix
    char *match_ptrs[1024];
    for (i = 0; i < match_count; i++) {
      match_ptrs[i] = matches[i];
    }
    
    char lcp[256];
    int lcp_len = find_longest_common_prefix(match_ptrs, match_count, lcp, sizeof(lcp));
    
    // If LCP is longer than current input, complete to LCP
    if (lcp_len > len) {
      // Complete to LCP
      for (i = len; i < lcp_len; i++) {
        buffer[i] = lcp[i];
        printf("%c", lcp[i]);
        fflush(stdout);
      }
      *pos = lcp_len;
      *tab_count = 0;  // Reset tab count after completion
    }
    // Otherwise, handle double-tab behavior
    else {
      if (*tab_count == 1) {
        // First tab: ring the bell
        printf("\x07");
        fflush(stdout);
      } else if (*tab_count >= 2) {
        // Second tab: display all matches
        // Sort matches alphabetically
        qsort(match_ptrs, match_count, sizeof(char *), string_compare);
        
        // Print newline and display matches
        printf("\n");
        for (i = 0; i < match_count; i++) {
          if (i > 0) {
            printf("  ");  // Two spaces between matches
          }
          printf("%s", match_ptrs[i]);
        }
        printf("\n");
        
        // Redisplay prompt and current command
        printf("$ ");
        for (i = 0; i < *pos; i++) {
          printf("%c", buffer[i]);
        }
        fflush(stdout);
        
        *tab_count = 0;  // Reset tab count after displaying matches
      }
    }
  }
}

// Read input with tab completion support
char* read_input_with_completion(const char *prompt) {
  static char buffer[1024];
  static int tab_count = 0;
  static char last_prefix[1024];
  static int last_prefix_len = 0;
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
      handle_tab_completion(buffer, &pos, &tab_count, last_prefix, &last_prefix_len);
    } else if (c == '\n') {
      // Enter key
      buffer[pos] = '\0';
      printf("\n");
      fflush(stdout);
      tab_count = 0;  // Reset tab count on enter
      break;
    } else if (c == 127 || c == 8) {
      // Backspace
      if (pos > 0) {
        pos--;
        printf("\b \b");
        fflush(stdout);
        tab_count = 0;  // Reset tab count on any other input
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
        tab_count = 0;  // Reset tab count on any other input
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

// Function to find the executable path in PATH
char* find_executable(const char *command, char *full_path, size_t path_size) {
  char *path_env = getenv("PATH");
  if (path_env == NULL) {
    return NULL;
  }
  
  char path_copy[4096];
  strncpy(path_copy, path_env, sizeof(path_copy) - 1);
  path_copy[sizeof(path_copy) - 1] = '\0';
  
  char *dir = strtok(path_copy, ":");
  while (dir != NULL) {
    snprintf(full_path, path_size, "%s/%s", dir, command);
    if (access(full_path, X_OK) == 0) {
      return full_path;
    }
    dir = strtok(NULL, ":");
  }
  
  return NULL;
}

// Check if a command is a built-in
int is_builtin(const char *cmd) {
  return (strcmp(cmd, "echo") == 0 || 
          strcmp(cmd, "exit") == 0 || 
          strcmp(cmd, "type") == 0 || 
          strcmp(cmd, "pwd") == 0 || 
          strcmp(cmd, "cd") == 0);
}

// Execute a built-in command with given arguments
void execute_builtin(char **args, int arg_count) {
  if (strcmp(args[0], "echo") == 0) {
    for (int i = 1; i < arg_count; i++) {
      if (i > 1) printf(" ");
      // Handle escape sequences
      char *str = args[i];
      for (int j = 0; str[j] != '\0'; j++) {
        if (str[j] == '\\' && str[j+1] == 'n') {
          printf("\n");
          j++;
        } else {
          printf("%c", str[j]);
        }
      }
    }
    printf("\n");
  } else if (strcmp(args[0], "pwd") == 0) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
      printf("%s\n", cwd);
    }
  } else if (strcmp(args[0], "type") == 0) {
    if (arg_count < 2) {
      return;
    }
    char *arg = args[1];
    
    if (is_builtin(arg)) {
      printf("%s is a shell builtin\n", arg);
    } else {
      // Search for executable in PATH
      char *path_env = getenv("PATH");
      if (path_env == NULL) {
        printf("%s: not found\n", arg);
        return;
      }
      
      char path_copy[4096];
      strncpy(path_copy, path_env, sizeof(path_copy) - 1);
      path_copy[sizeof(path_copy) - 1] = '\0';
      
      char *dir = strtok(path_copy, ":");
      int found = 0;
      
      while (dir != NULL) {
        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, arg);
        
        if (access(full_path, X_OK) == 0) {
          printf("%s is %s\n", arg, full_path);
          found = 1;
          break;
        }
        
        dir = strtok(NULL, ":");
      }
      
      if (!found) {
        printf("%s: not found\n", arg);
      }
    }
  }
}

// Execute a pipeline with two commands
int execute_pipeline(char *cmd1, char *cmd2) {
  // Parse both commands - need separate storage for each
  static char arg_buffer1[64][1024];
  static char arg_buffer2[64][1024];
  char *args1[64];
  char *args2[64];
  
  // Parse first command manually to avoid static buffer issues
  int arg_count1 = 0;
  int i = 0;
  int len = strlen(cmd1);
  
  while (i < len && arg_count1 < 63) {
    while (i < len && isspace(cmd1[i])) i++;
    if (i >= len) break;
    
    int arg_len = 0;
    while (i < len && !isspace(cmd1[i]) && arg_len < 1023) {
      arg_buffer1[arg_count1][arg_len++] = cmd1[i++];
    }
    arg_buffer1[arg_count1][arg_len] = '\0';
    args1[arg_count1] = arg_buffer1[arg_count1];
    arg_count1++;
  }
  args1[arg_count1] = NULL;
  
  // Parse second command manually
  int arg_count2 = 0;
  i = 0;
  len = strlen(cmd2);
  
  while (i < len && arg_count2 < 63) {
    while (i < len && isspace(cmd2[i])) i++;
    if (i >= len) break;
    
    int arg_len = 0;
    while (i < len && !isspace(cmd2[i]) && arg_len < 1023) {
      arg_buffer2[arg_count2][arg_len++] = cmd2[i++];
    }
    arg_buffer2[arg_count2][arg_len] = '\0';
    args2[arg_count2] = arg_buffer2[arg_count2];
    arg_count2++;
  }
  args2[arg_count2] = NULL;
  
  if (arg_count1 == 0 || arg_count2 == 0) {
    return -1;
  }
  
  // Check if commands are built-ins
  int cmd1_is_builtin = is_builtin(args1[0]);
  int cmd2_is_builtin = is_builtin(args2[0]);
  
  // Find executables (only for non-built-ins)
  char full_path1[2048];
  char full_path2[2048];
  
  if (!cmd1_is_builtin) {
    if (find_executable(args1[0], full_path1, sizeof(full_path1)) == NULL) {
      printf("%s: command not found\n", args1[0]);
      return -1;
    }
  }
  
  if (!cmd2_is_builtin) {
    if (find_executable(args2[0], full_path2, sizeof(full_path2)) == NULL) {
      printf("%s: command not found\n", args2[0]);
      return -1;
    }
  }
  
  // Create pipe
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    perror("pipe");
    return -1;
  }
  
  // Fork first command
  pid_t pid1 = fork();
  if (pid1 == -1) {
    perror("fork");
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }
  
  if (pid1 == 0) {
    // First child process
    // Redirect stdout to pipe write end
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[0]);  // Close unused read end
    close(pipefd[1]);  // Close original write end
    
    if (cmd1_is_builtin) {
      execute_builtin(args1, arg_count1);
      exit(0);
    } else {
      execv(full_path1, args1);
      perror("execv");
      exit(1);
    }
  }
  
  // Fork second command
  pid_t pid2 = fork();
  if (pid2 == -1) {
    perror("fork");
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }
  
  if (pid2 == 0) {
    // Second child process
    // Redirect stdin to pipe read end
    dup2(pipefd[0], STDIN_FILENO);
    close(pipefd[1]);  // Close unused write end
    close(pipefd[0]);  // Close original read end
    
    if (cmd2_is_builtin) {
      execute_builtin(args2, arg_count2);
      exit(0);
    } else {
      execv(full_path2, args2);
      perror("execv");
      exit(1);
    }
  }
  
  // Parent process
  close(pipefd[0]);
  close(pipefd[1]);
  
  // Wait for both children
  waitpid(pid1, NULL, 0);
  waitpid(pid2, NULL, 0);
  
  return 0;
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

  // Check for pipe operator
  char *pipe_pos = strchr(command, '|');
  if (pipe_pos != NULL) {
    // Split the command at the pipe
    *pipe_pos = '\0';  // Null-terminate the first command
    char *cmd1 = command;
    char *cmd2 = pipe_pos + 1;
    
    // Trim leading whitespace from cmd2
    while (*cmd2 == ' ' || *cmd2 == '\t') {
      cmd2++;
    }
    
    // Trim trailing whitespace from cmd1
    char *end = pipe_pos - 1;
    while (end > cmd1 && (*end == ' ' || *end == '\t')) {
      *end = '\0';
      end--;
    }
    
    // Execute pipeline
    execute_pipeline(cmd1, cmd2);
    continue;
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