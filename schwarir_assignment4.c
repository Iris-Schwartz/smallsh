/*
TO DO:
 -- reminder to change all names to snake_case
 -- upon exit from smallsh, smallsh must kill any other processes or jobs that shell has started  before it terminates itself (i.e. background processes)
-- test functionality of cd command
-- change message printed with # command
-- add fflush where helpful to print stdout buffer contents to terminal
-- add message about background process pid when background process started
-- add the following signal handling: 

-----------------------------SIGINT-------------------------------SIGTSSP

shell                        ignore                                ***

background_process           ignore                               ignore

foreground_process         foreground process                     ignore
                             terminates itself
                            and parent immediately
                             prints which
                          signal killed foreground process

*** The shell must display an informative message (see below) immediately if it's sitting at the prompt, or immediately after any currently running foreground process has terminated
The shell then enters a state where subsequent commands can no longer be run in the background.
In this state, the & operator must simply be ignored, i.e., all such commands are run as if they were foreground processes.

If the user sends SIGTSTP again, then your shell will
Display another informative message (see below) immediately after any currently running foreground process terminates
The shell then returns back to the normal condition where the & operator is once again honored for subsequent commands, allowing them to be executed in the background.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define INPUT_LENGTH 2048
#define MAX_ARGS 512

struct command_line
{
  char *argv[MAX_ARGS + 1];
  int argc;
  char *input_file;
  char *output_file;
  bool is_bg;
};

struct background_process
{
  pid_t background_process_pid;
  char *background_process_name;
};

int last_index_in_background_processes_array = 0;
struct background_process background_processes[512];
int has_run_foreground_command = 0;
int lastExitStatus = 0;

void redirect(struct command_line *curr_command);
int background_command(struct command_line *curr_command);
int foreground_command(struct command_line *curr_command);
// void handle_SIGTSTP(int signal);

struct command_line *parse_input()
{
  char input[INPUT_LENGTH];
  struct command_line *curr_command = (struct command_line *)calloc(1, sizeof(struct command_line));

  // Get input
  printf(": ");
  fflush(stdout);
  fgets(input, INPUT_LENGTH, stdin);

  // Tokenize the input
  char *token = strtok(input, " \n");
  while (token)
  {
    if (!strcmp(token, "<"))
    {
      curr_command->input_file = strdup(strtok(NULL, " \n"));
    }
    else if (!strcmp(token, ">"))
    {
      curr_command->output_file = strdup(strtok(NULL, " \n"));
    }
    else if (!strcmp(token, "&"))
    {
      curr_command->is_bg = true;
    }
    else
    {
      curr_command->argv[curr_command->argc++] = strdup(token);
    }
    token = strtok(NULL, " \n");
  }
  return curr_command;
}

int main()
{
  struct command_line *curr_command;

  while (true)
  {
    curr_command = parse_input();
    if (curr_command->argc == 0 || curr_command->argv[0][0] == '#')
    {
      printf("Comment or blank line found\n");
      continue;
    }
    else if (strcmp(curr_command->argv[0], "status") == 0)
    {
      if (has_run_foreground_command == 0)
      {
        printf("exit value 0\n");
      }
      else
      {
        printf("exit value %d\n", lastExitStatus);
      }
    }
    else if (strcmp(curr_command->argv[0], "exit") == 0)
    {
      exit(0);
    }
    else if (strcmp(curr_command->argv[0], "cd") == 0)
    {
      if (curr_command->argv[1] != NULL)
      {
        chdir(curr_command->argv[1]);
        perror("Issue changing to a certain directory");
      }
      else
      {
        chdir(getenv("HOME"));
        perror("Issue changing to home directory");
      }
    }
    else
    {
      if (curr_command->is_bg)
      {
        background_command(curr_command);
      }
      else
      {
        lastExitStatus = foreground_command(curr_command);
      }
    }

    int childStatus;
    int childPid;
    // the shell constantly checks if any background child process just finished and reaps it
    // (removes it from ps table) if so before the next command prompt is printed
    // assistance was provided by TA to generate the proper while loop header
    while ((childPid = waitpid(-1, &childStatus, WNOHANG)) > 0)
    {
      if (WIFEXITED(childStatus))
      {
        for (int i = 0; i < 512; i++)
        {
          if (background_processes[i].background_process_pid == childPid)
          {
            printf("background pid %d is done: exit value 0\n", background_processes[i].background_process_pid);
            printf(" # the background %s finally finished\n", background_processes[i].background_process_name);
          }
        }
      }
      else
      {
        printf(WTERMSIG(childStatus));
      }
    }
  }
  return EXIT_SUCCESS; // exits from shell (parent process)
}

void redirect(struct command_line *curr_command)
{
  if (curr_command->output_file != NULL)
  {
    int fd1 = open(curr_command->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd1 == -1)
    {
      printf("cannot open %s for output", curr_command->output_file);
      exit(1);
    }
    if (dup2(fd1, 1) == -1)
    {
      printf("Error redirecting stdout to output file.");
      exit(1);
    };
    if (curr_command->is_bg)
    {
      int fd2 = open("/dev/null", O_WRONLY);
      if (dup2(fd2, 0) == -1)
      {
        printf("Error redirecting stdin to input file.");
        exit(1);
      };
    }
    close(fd1);
  }
  if (curr_command->input_file != NULL)
  {
    int fd2 = open(curr_command->input_file, O_RDONLY);
    if (fd2 == -1)
    {
      printf("cannot open %s for input", curr_command->input_file);
      exit(1);
    }
    if (dup2(fd2, 0) == -1)
    {
      printf("Error redirecting stdin to input file.");
      exit(1);
    };
    if (curr_command->is_bg)
    {
      int fd2 = open("/dev/null", O_RDONLY);
      if (dup2(fd2, 1) == -1)
      {
        printf("Error redirecting stdout to output file.");
        exit(1);
      };
    }
    close(fd2);
  }
}

// void handle_SIGTSTP(int signal)
// {
// }

int background_command(struct command_line *curr_command)
{
  pid_t pidOfChild = fork();

  background_processes[last_index_in_background_processes_array + 1].background_process_pid = pidOfChild;
  background_processes[last_index_in_background_processes_array + 1].background_process_name = curr_command->argv[0];

  last_index_in_background_processes_array++;

  switch (pidOfChild)
  {
  case -1:
    perror("issue with fork()");
    exit(1);
  case 0:
    redirect(curr_command);
    if (execvp(curr_command->argv[0], curr_command->argv) == -1)
    {
      printf("%s: no such file or directory\n", curr_command->argv[0]);
      exit(1);
    }
    break;
  default:
  }
}

int foreground_command(struct command_line *curr_command)
{
  int childStatus;
  has_run_foreground_command = 1; // needs to be updated in the parent b/c child update won't get remembered by parent

  pid_t pidOfChild = fork();

  switch (pidOfChild)
  {
  case -1:
    perror("issue with fork()");
    exit(1);
  case 0:
    redirect(curr_command);
    if (execvp(curr_command->argv[0], curr_command->argv) == -1)
    {
      printf("%s: no such file or directory\n", curr_command->argv[0]);
      exit(1);
    }
    break;
  default:
    pidOfChild = waitpid(pidOfChild, &childStatus, 0);
    if (WIFEXITED(childStatus))
    {
      return WEXITSTATUS(childStatus);
    }
    else
    {
      return WTERMSIG(childStatus);
    }
  }
}
