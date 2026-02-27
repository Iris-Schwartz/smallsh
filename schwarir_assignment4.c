/*
TO DO:
-- reminder to change all names to snake_case
-- add fflush where helpful to print stdout buffer contents to terminal
-- how to prevent "Terminated" to print upon exit from smallsh shell
-- Debug why cd command with and without arguments fails
-- Ask TA how to get the right message after # printed on same line versus the next line
-- ask TA how to handle if blank line and whether it's necessary to print something
-- add the following signal handling (✅ if finished)

-----------------------------SIGINT-------------------------------SIGTSTP

shell                        ignore  ✅                            ***

background_process           ignore    ✅ inherited from shell     ignore

foreground_process         foreground process                     ignore
                             terminates itself✅

                            parent immediately
                             prints which
                          signal killed foreground process

*** The shell must display an informative message (see below) immediately if it's sitting at the prompt, or immediately after any currently running foreground process has terminated
The shell then enters a state where subsequent commands can no longer be run in the background.
In this state, the & operator must simply be ignored, i.e., all such commands are run as if they were foreground processes.

If the user sends SIGTSTP again, then your shell will
Display another informative message (see below) immediately after any currently running foreground process terminates
The shell then returns back to the normal condition where the & operator is once again honored for subsequent commands, allowing them to be executed in the background.
*/

#define _POSIX_C_SOURCE 200809L // ChatGPT recommended adding this due to issues creating struct sigaction instance

#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
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

// this function was provided as started code by Professor Tonsmann from OSU's CS 374 class for
// the purposes of this assignment
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

  // in discussing my reasoning for installing the ignore_signal signal handler right after the printing of the smallsh : command prompt (to ignore SIGINT in the shell) ChatGPT provided a strong argument for installing it at the start of main

  // program code for signal handler installation was adapted from OSU's CS 374's "Exploration: Signal Handling API" lesson
  // in module 7 (provided by Professor Tonsmann)
  struct sigaction ignore_signal = {0};

  ignore_signal.sa_handler = SIG_IGN;
  sigfillset(&ignore_signal.sa_mask);
  ignore_signal.sa_flags = 0;

  sigaction(SIGINT, &ignore_signal, NULL);

  while (true)
  {
    curr_command = parse_input();
    if (curr_command->argc == 0 || curr_command->argv[0][0] == '#')
    {
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
      // kills all background processes that shell has previously started before it terminates itself (note: they will eventually also be reaped, or removed from ps table, by init, since parent process does not reap them here)
      for (int i = 0; i <= last_index_in_background_processes_array; i++)
      {
        kill(background_processes[i].background_process_pid, SIGTERM);
      }
      exit(0);
    }
    else if (strcmp(curr_command->argv[0], "cd") == 0)
    {
      if (curr_command->argv[1] != NULL)
      {
        if (chdir(curr_command->argv[1]) == -1)
        {
          perror("Issue changing to a certain directory");
        }
      }
      else
      {
        if (chdir(getenv("HOME")) == -1)
        {
          perror("Issue changing to home directory");
        };
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
            printf("the background %s finally finished\n", background_processes[i].background_process_name);
          }
        }
      }
      else
      {
        printf("%d", WTERMSIG(childStatus));
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
    background_processes[last_index_in_background_processes_array + 1].background_process_pid = pidOfChild;
    background_processes[last_index_in_background_processes_array + 1].background_process_name = curr_command->argv[0];
    last_index_in_background_processes_array++;
    printf("background pid is %d\n", pidOfChild);
  }
}

int foreground_command(struct command_line *curr_command)
{
  int childStatus;
  has_run_foreground_command = 1; // needs to be updated in the parent b/c child update won't get remembered by parent

  struct sigaction reverse_ignore_from_shell = {0};
  reverse_ignore_from_shell.sa_handler = SIG_DFL;

  pid_t pidOfChild = fork();

  // new signal handler needed for default action of SIGINT b/c signal handler that ignores SIGINT was inherited from parent process

  switch (pidOfChild)
  {
  case -1:
    perror("issue with fork()");
    exit(1);
  case 0:
    sigaction(SIGINT, &reverse_ignore_from_shell, NULL);
    redirect(curr_command);
    if (execvp(curr_command->argv[0], curr_command->argv) == -1)
    {
      printf("%s: no such file or directory\n", curr_command->argv[0]);
      exit(1);
    }
    break;
  default:
    pidOfChild = waitpid(pidOfChild, &childStatus, 0);
    if (WIFEXITED(childStatus)) // if child terminated normally
    {
      return WEXITSTATUS(childStatus); // exit value the child passed to exit()
    }
    else // if child terminated abnormally
    {
      int sigNum = WTERMSIG(childStatus);
      printf("terminated by signal %d\n", sigNum);
      fflush(stdout);
      return sigNum; // signal number that caused the child to terminate
    }
  }
}
