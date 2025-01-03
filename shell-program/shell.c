
/*
Acknowledgement
1. https://stackoverflow.com/questions/21248840/example-of-waitpid-in-use 
2. https://www.geeksforgeeks.org/c-program-demonstrate-fork-and-pipe/ 
Provided by TA Emos Ker
3. https://www.gnu.org/software/libc/manual/html_node/Process-Completion.html 
4. https://www.gnu.org/software/libc/manual/html_node/Process-Completion-Status.html 
5. https://docs.oracle.com/cd/E19455-01/817-5438/6mkt5pciv/index.html 
6. https://pubs.opengroup.org/onlinepubs/009695299/functions/access.html 
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

void print_prompt(void);
void command_loop(void);
void handle_builtin(char *command);
void handle_external_single_command(char *command);
void handle_singe_pipe(char* command);
void handle_multiple_pipes(char * command);
void signal_handling(void);
void signal_handler(int sig);
void handle_suspension(pid_t pid, char *command);
void add_suspended_job(pid_t pid, char *command);

// built-in commands: cd, jobs, fg, exit
// external commands: redirection & pipes

// signal handling
// Use signal() or sigaction() 
// to set up custom signal handling for SIGINT, SIGQUIT, and SIGTSTP


typedef struct {
  int id;
  pid_t pid;  // Process ID for job control
  char command[1000];
} Job;

// initiate suspended jobs array
Job suspended_jobs[100];
int job_count = 0;
pid_t foreground_pid = -1;

int main() {
  signal_handling();
  command_loop();
  return 0;
}
// command loop
// print prompt

/*
Milestone 1. Write a simple program that prints the prompt and flushes STDOUT. 
You may use the getcwd() system call to get the current working directory.
*/
void print_prompt(void) {
  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    if (strcmp(cwd, "/") == 0) {
      printf("[nyush /]$ ");
    } 
    else {
      char *current_folder = strrchr(cwd, '/');
      if (current_folder != NULL) {
        printf("[nyush %s]$ ", current_folder + 1);
      }
    }
    fflush(stdout);
  }
}

void command_loop(void) {
  char *command = NULL;
  size_t buffer_size = 0;
  ssize_t characters_number;

  while (1) {
    signal_handling();
    print_prompt();
    characters_number = getline(&command, &buffer_size, stdin);
    if (characters_number == -1) {
      break;
    }
    command[strcspn(command, "\n")] = '\0';
    if (strcmp(command, "") == 0) {
      continue;
    }

    // Built-in Commands
    if (strncmp(command, "cd", 2) == 0 || strncmp(command, "fg", 2) == 0 || strcmp(command, "jobs") == 0 || strcmp(command, "exit") == 0) {
      handle_builtin(command);
    } 
    else if (strstr(command, "|") != NULL) {
      handle_multiple_pipes(command);
    }
    else {
      // External Commands
      handle_external_single_command(command);
    }
  }
  free(command);
}

/*
Milestone 3. Extend Milestone 2 to be able to run a simple program, such as ls.
while (true) {
  // print prompt
  // read command
  pid = fork();
  if (pid < 0) {
    // fork failed (this shouldn't happen)
  } else if (pid == 0) {
    // child (new process)
    execv(...);  // or another variant
    // exec failed
  } else {
    // parent
    waitpid(-1, ...);
  }
}
*/
void handle_builtin(char *command) {
  char *args[1000];
  int i = 0;

  // Tokenize the command to separate arguments
  char *token = strtok(command, " ");
  while (token != NULL && i < 1000) {
    args[i++] = token;
    token = strtok(NULL, " ");
  }
  args[i] = NULL;  // Null-terminate the argument list

  char* first_arg = args[0];
  char* second_arg = args[1];
  char* third_arg = args[2];

  // Built-in Commands
  /*
  Milestone 5. Handle simple built-in commands (cd and exit). 
  You may use the chdir() system call to change the working directory.
  */
  // cd <dir> command
  if (strcmp(first_arg, "cd") == 0) {
    if (second_arg == NULL || third_arg != NULL) {
      fprintf(stderr, "Error: invalid command\n");
      return;
    }
    int run_cd = chdir(second_arg);
    if (run_cd != 0) {
      fprintf(stderr, "Error: invalid directory\n");
      return;
    }
  }
  // exit command
  else if (strcmp(first_arg, "exit") == 0) {
    if (second_arg != NULL) {
      fprintf(stderr, "Error: invalid command\n");
      return;
    }
    if (job_count > 0) {
      fprintf(stderr, "Error: there are suspended jobs\n");
      return;
    }
    exit(0);
  }
  /*
  Milestone 10. Handle suspended jobs and related built-in commands (jobs and fg). 
  Read man waitpid to see how to wait for a stopped child.
  */
  // fg <index> command
  else if (strcmp(first_arg, "fg") == 0) {
    if (second_arg == NULL || third_arg != NULL) {
      fprintf(stderr, "Error: invalid command\n");
      return;
    }

    int job_index = atoi(second_arg) - 1;
    if (job_index < 0 || job_index >= job_count) {
      fprintf(stderr, "Error: invalid job\n");
      return;
    }

    // Resume the specified job in the foreground
    pid_t pid = suspended_jobs[job_index].pid;

    // Remove the job from the list and shift remaining jobs up
    for (int i = job_index; i < job_count - 1; i++) {
      suspended_jobs[i] = suspended_jobs[i + 1];
      suspended_jobs[i].id = i + 1;
    }
    job_count--;

    foreground_pid = pid;

    kill(pid, SIGCONT);

    int status;
    waitpid(pid, &status, WUNTRACED);
    if (WIFSTOPPED(status)) {
        add_suspended_job(pid, command);
    }

    foreground_pid = -1;
  }

  // jobs command
  else if (strcmp(first_arg, "jobs") == 0) {
    /*
    if (second_arg != NULL || third_arg != NULL) {
      fprintf(stderr, "Error: invalid command\n");
      return;
    }
    */
    for (int i = 0; i < job_count; i++) {
      // [index] command
      printf("[%d] %s\n", i + 1, suspended_jobs[i].command);
    }
    fflush(stdout);
  }
}

void handle_external_single_command(char *command) {
  char *args[1000];
  int i = 0;
  int append_flag = 0;
  char *input_filename = NULL;
  char *output_filename = NULL;

  char command_copy[1000];
  strncpy(command_copy, command, sizeof(command_copy) - 1);
  command_copy[sizeof(command_copy) - 1] = '\0';

  // Tokenize the command
  char *token = strtok(command, " ");
  while (token != NULL && i < 1000) {
    /*
    Milestone 6. Handle output redirection, such as cat > output.txt.
    Milestone 7. Handle input redirection, such as cat < input.txt.
    */
    if (strcmp(token, "<") == 0) {
      token = strtok(NULL, " ");
      input_filename = token;
    } 
    else if (strcmp(token, ">") == 0 || strcmp(token, ">>") == 0) {
      if (strcmp(token, ">>") == 0) {
        append_flag = 1;
      }
      token = strtok(NULL, " ");
      output_filename = token;
    }
    else {
      args[i++] = token;
    }
    token = strtok(NULL, " ");
  }
  args[i] = NULL;  // Null-terminate the argument list

  // Path Handling
  char path[PATH_MAX];
  if (strchr(args[0], '/') != NULL) {
    // Absolute or relative path
    strcpy(path, args[0]);
  } else {
    // Base name command (search /usr/bin/)
    strcpy(path, "/usr/bin/");
    strcat(path, args[0]);
  }

  pid_t pid = fork();
  if (pid < 0) {
    perror("Fork failed");

  } else if (pid == 0) {
    // Note that only the shell itself, not the child processes created by the shell, 
    // should ignore these signals
    // child process hnadle sigint, sigquit, and sigstop
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL); 
    signal(SIGTSTP, SIG_DFL);

    
    if (input_filename != NULL) {
      // "<" redirect input
      // I/O redirection and pipes: dup2(), creat(), open(), close(), pipe()
      int input = open(input_filename, O_RDONLY);
      // if file cannot be opened
      if (input < 0) {
        fprintf(stderr, "Error: invalid file\n");
        exit(-1);
      }

      dup2(input, STDIN_FILENO);  // Redirect stdin
      close(input);
    }
    if (output_filename != NULL) {
      int output;
      if (append_flag == 1) {
        output = open(output_filename, O_WRONLY | O_CREAT | O_APPEND);
      }
      else {
        output = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC);
      }
      // if file cannot be opened
      if (output < 0) {
        fprintf(stderr, "Error: invalid file\n");
        exit(-1);
      }

      dup2(output, STDOUT_FILENO);  // Redirect stdout
      close(output);
    }
    if (execv(path, args) == -1) {
      fprintf(stderr, "Error: invalid program\n");
      exit(-1);
    } 
  } 
  else {  // Parent process waits for the child to finish or stop
    foreground_pid = pid; 
    handle_suspension(pid, command_copy);
    foreground_pid = -1;
  }
}

/*
Milestone 8. Run two programs with one pipe, such as cat | cat. 
The example code in man 2 pipe is very helpful.
*/
void handle_singe_pipe(char* command) {
  char *commands[2];
  commands[0] = strtok(command, "|"); // everything before |
  commands[1] = strtok(NULL, "|");// everything after |
  // 2 file descriptor
  int fd[2]; // read & write
  // fd[0]; // using for read end
  // fd[1]; // using for write end

  if (pipe(fd) == -1) {
    return;
  }
  pid_t pid1;
  pid_t pid2;

  // first child
  pid1 = fork();
  if (pid1 < 0) {
    return;
  } 
  else if (pid1 == 0) {
    // Redirect stdout to the write
    dup2(fd[1], STDOUT_FILENO);
    close(fd[0]);
    close(fd[1]);
    handle_external_single_command(commands[0]);
    exit(-1);
  }
  
  // second child
  pid2 = fork();
  if (pid2 < 0) {
    return;
  } else if (pid2 == 0) {
    // Redirect stdin to the read
    dup2(fd[0], STDIN_FILENO);
    close(fd[1]);
    close(fd[0]);
    handle_external_single_command(commands[1]);
    exit(-1);
  }
  close(fd[0]);
  close(fd[1]);
  waitpid(pid1, NULL, 0);
  waitpid(pid2, NULL, 0);
}
/*
Milestone 9. Handle multiple pipes, such as cat | cat | cat.
*/
void handle_multiple_pipes(char* command) {
  char *commands[100];
  int command_number = 0;
  
  // Split the command by '|'
  char *token = strtok(command, "|");
  while (token != NULL && command_number < 100) {
    commands[command_number++] = token;
    token = strtok(NULL, "|");
  }

  // 2-D pipe array [0] is for read and [1] is to write
  int pipe_fd[command_number - 1][2]; // Create pipes based on number of commands
  
  for (int i = 0; i < command_number - 1; i++) {
    if (pipe(pipe_fd[i]) == -1) {
      exit(EXIT_FAILURE);
    }
  }

  for (int i = 0; i < command_number; i++) {
    pid_t pid = fork();
    
    if (pid < 0) {
      exit(EXIT_FAILURE);
    }
    else if (pid == 0) {
      if (i > 0) {
        dup2(pipe_fd[i - 1][0], STDIN_FILENO);
      }
      if (i < command_number - 1) {
        dup2(pipe_fd[i][1], STDOUT_FILENO);
      }
      // Close all pipe file descriptors in the child
      for (int j = 0; j < command_number - 1; j++) {
        close(pipe_fd[j][0]);
        close(pipe_fd[j][1]);
      }
      handle_external_single_command(commands[i]);
      exit(EXIT_FAILURE);
    }
  }
  // Parent process closes all pipe file descriptors
  for (int i = 0; i < command_number - 1; i++) {
    close(pipe_fd[i][0]);
    close(pipe_fd[i][1]);
  }
  for (int i = 0; i < command_number; i++) { // Child process
    wait(NULL);
  }
}


/*
Milestone 10. Handle suspended jobs and related built-in commands (jobs and fg). 
Read man waitpid to see how to wait for a stopped child.
*/
// SHELL IGNORES THESE SIGNALS
void signal_handling() {
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGTSTP, signal_handler);
}

void signal_handler(int sig) {
    if (foreground_pid > 0 && sig == SIGTSTP) {
        kill(foreground_pid, SIGTSTP);
    }
}

void handle_suspension(pid_t pid, char *command) {
  int status;
  waitpid(pid, &status, WUNTRACED);
  if (WIFSTOPPED(status)) {
      add_suspended_job(pid, command);
  }
}

void add_suspended_job(pid_t pid, char *command) {
  suspended_jobs[job_count].id = job_count + 1;
  suspended_jobs[job_count].pid = pid;
  strncpy(suspended_jobs[job_count].command, command, sizeof(suspended_jobs[job_count].command) - 1);
  suspended_jobs[job_count].command[sizeof(suspended_jobs[job_count].command) - 1] = '\0';
  job_count++;
}

/*
Running Docker:
docker run -i --name cs202 --privileged --rm -t -v ~/Desktop/cs202/labs:/cs202 -w /cs202 ytang/os bash
gcc -o nyush nyush.c
./nyush
*/

/*
Running MakeFile:
make run
*/

// pid takes status
