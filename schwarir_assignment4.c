/*
Author: Iris Schwartz
OSU Email Address: schwarir@oregonstate.edu
Course Number: CS 374
Programming Assignment: 4
Due Date: 03/01/26
Description: This program creates a shell called smallsh that, based on input prompted by the command prompt :, can three built-in commands of cd, exit and status and can fork foreground and background child processes that execute the programs of files related to other commands (such as sleep, ls, ps, kill, etc.). This program also handles redirection of stdout and stdin based on < and > in the input, respectively. In addition, this program handles signals by either ignoring them, implenting their default actions or changing their disposition with custom signal handlers. Of note, the shell registers
and installs a custom signal handler for toggling between regular mode and foreground-only mode, where background commands (indicated with &) are interpreted as foreground commands. Lastly, before the next command prompt : is printed, the parent shell checks for children that have terminated but are still in ps table and reaps (removes from ps table) these terminated children with waitpid() system call.
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
int background_processes[512];
int has_run_foreground_command = 0;
int last_exit_status = 0;
int toggle_value = 0;

void redirect(struct command_line *curr_command);
int background_command(struct command_line *curr_command);
int foreground_command(struct command_line *curr_command);
void SIGTSTP_prevent_background_commmands_toggle(int signal);
struct command_line *parse_input();

int main()
{
  struct command_line *curr_command;

  // in discussing reasoning for registering and installing the SIGINT shell signal handler right after the printing of the smallsh : command prompt (to ignore SIGINT in the shell) ChatGPT provided a strong argument for installing it at the start of main

  // program code for signal handler installation was adapted from OSU's CS 374's "Exploration: Signal Handling API" lesson
  // in module 7 (provided by Professor Tonsmann)

  // registering the CTRL-C or SIGINT shell signal handler
  struct sigaction ignore_SIGINT_signal = {0};
  ignore_SIGINT_signal.sa_handler = SIG_IGN;
  sigfillset(&ignore_SIGINT_signal.sa_mask);
  ignore_SIGINT_signal.sa_flags = 0;

  // installing the CTRL-C or SIGINT shell signal handler
  sigaction(SIGINT, &ignore_SIGINT_signal, NULL);

  // registering signal handler for CTRL-Z (i.e. SIGTSTP) toggle of foreground-only mode
  struct sigaction SIGTSTP_handler = {0};
  sigfillset(&SIGTSTP_handler.sa_mask);
  SIGTSTP_handler.sa_handler = SIGTSTP_prevent_background_commmands_toggle;
  // suggested by TA to solve issue of fgets not being restarted from where it left off when it gets interrupted by CTRL-Z and instead of being restarted, returning NULL and executing a previous command entered in smallsh shell (vs current command)
  SIGTSTP_handler.sa_flags = SA_RESTART;

  // install CTRL-Z foreground-only mode toggle signal handler
  sigaction(SIGTSTP, &SIGTSTP_handler, NULL);

  while (true)
  {
    // the shell constantly checks if any background child process just finished before parent shell (i.e. a zombie) and reaps it
    // (removes it from ps table) if so before the next command prompt is printed
    // assistance was provided by TA to generate the proper while loop header

    // among the various ideas ChatGPT gave in solving the issue of exit value of background process
    // not printing right away, of which multiple I tried implenting, was looking into whether
    // the exit value of a background process that just finished wasn't finished until after the
    // next command prompt : of smallsh shell was printed

    int child_status;
    int child_pid;

    while ((child_pid = waitpid(-1, &child_status, WNOHANG)) > 0)
    {
      if (WIFEXITED(child_status))
      {
        printf("background pid %d is done: exit value %d\n", child_pid, WEXITSTATUS(child_status));
        fflush(stdout);
      }
      else
      {
        printf("background pid %d is done: terminated by signal %d\n", child_pid, WTERMSIG(child_status));
        fflush(stdout);
      }
    }
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
        fflush(stdout);
      }
      else
      {
        printf("exit value %d\n", last_exit_status);
        fflush(stdout);
      }
    }
    else if (strcmp(curr_command->argv[0], "exit") == 0)
    {
      // kills all background processes that shell has previously started before the shell terminates itself (note: they will eventually also be reaped, or removed from ps table, by init, since parent process does not reap them here)
      for (int i = 0; i <= last_index_in_background_processes_array; i++)
      {
        kill(background_processes[i], SIGTERM);
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
      // short circuit evaluation so that foreground command executed as
      // soon as toggle_value == 0 evaluates to false regardless of value of curr_command->is_bg
      if (toggle_value == 0 && curr_command->is_bg)
      {
        background_command(curr_command);
      }
      else
      {
        last_exit_status = foreground_command(curr_command);
      }
    }
  }
  return EXIT_SUCCESS; // exits from shell (parent process)
}

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

void redirect(struct command_line *curr_command)
{
  if (curr_command->output_file != NULL)
  {
    int fd1 = open(curr_command->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd1 == -1)
    {
      printf("cannot open %s for output", curr_command->output_file);
      fflush(stdout);
      exit(1);
    }
    if (dup2(fd1, 1) == -1)
    {
      printf("Error redirecting stdout to output file.");
      fflush(stdout);
      exit(1);
    };
    if (curr_command->is_bg)
    {
      int fd2 = open("/dev/null", O_WRONLY);
      if (dup2(fd2, 0) == -1)
      {
        printf("Error redirecting stdin to input file.");
        fflush(stdout);
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
      fflush(stdout);
      exit(1);
    }
    if (dup2(fd2, 0) == -1)
    {
      printf("Error redirecting stdin to input file.");
      fflush(stdout);
      exit(1);
    };
    if (curr_command->is_bg)
    {
      int fd2 = open("/dev/null", O_RDONLY);
      if (dup2(fd2, 1) == -1)
      {
        printf("Error redirecting stdout to output file.");
        fflush(stdout);
        exit(1);
      };
    }
    close(fd2);
  }
}

void SIGTSTP_prevent_background_commmands_toggle(int signal)
{
  // if currently not in foreground only mode, then in response to SIGTSTP, switch to foreground
  // only mode
  if (toggle_value == 0)
  {
    toggle_value = 1;
    // reentrant function used (not printf())
    write(1, "\nEntering foreground-only mode (& is now ignored)", 51);
  }
  // if currently not in foreground only mode, then in response to SIGTSTP, switch to foreground
  // only mode
  else
  {
    toggle_value = 0;
    write(1, "\nExiting foreground-only mode", 31);
  }
}

int background_command(struct command_line *curr_command)
{
  // registering signal handler to ignore CTRL-Z to background command
  struct sigaction ignore_SIGTSTP_signal = {0};
  ignore_SIGTSTP_signal.sa_handler = SIG_IGN;
  sigfillset(&ignore_SIGTSTP_signal.sa_mask);
  ignore_SIGTSTP_signal.sa_flags = 0;

  pid_t child_pid = fork();

  switch (child_pid)
  {
  case -1:
    perror("issue with fork()");
    exit(1);
  case 0:
    // installing signal handler to ignore CTRL-Z to background command
    sigaction(SIGTSTP, &ignore_SIGTSTP_signal, NULL);
    redirect(curr_command);
    if (execvp(curr_command->argv[0], curr_command->argv) == -1)
    {
      printf("%s: no such file or directory\n", curr_command->argv[0]);
      fflush(stdout);
      exit(1);
    }
    break;
  default:
    background_processes[last_index_in_background_processes_array] = child_pid;
    last_index_in_background_processes_array++;
    printf("background pid is %d\n", child_pid);
    fflush(stdout);
  }
}

int foreground_command(struct command_line *curr_command)
{
  int child_status;
  has_run_foreground_command = 1; // needs to be updated in the parent b/c child update won't get remembered by parent

  // registering signal handler to reverse the ignoring of CTRL-C from shell parent process
  struct sigaction reverse_ignore_from_shell = {0};
  reverse_ignore_from_shell.sa_handler = SIG_DFL;

  // registering signal handler to ignore CTRL-Z to foreground command
  struct sigaction ignore_SIGTSTP_signal = {0};
  ignore_SIGTSTP_signal.sa_handler = SIG_IGN;
  sigfillset(&ignore_SIGTSTP_signal.sa_mask);
  ignore_SIGTSTP_signal.sa_flags = 0;

  pid_t child_pid = fork();

  switch (child_pid)
  {
  case -1:
    perror("issue with fork()");
    exit(1);
  case 0:
    // installing signal handler to reverse the ignoring of CTRL-C from shell parent process
    sigaction(SIGINT, &reverse_ignore_from_shell, NULL);
    // installing signal handler to ignore CTRL-Z to foreground command
    sigaction(SIGTSTP, &ignore_SIGTSTP_signal, NULL);
    redirect(curr_command);
    if (execvp(curr_command->argv[0], curr_command->argv) == -1)
    {
      printf("%s: no such file or directory\n", curr_command->argv[0]);
      fflush(stdout);
      exit(1);
    }
    break;
  default:
    child_pid = waitpid(child_pid, &child_status, 0); // shell is blocked; waits for foreground child process to finish before returning command line access;
    if (WIFEXITED(child_status))                      // if child terminated normally
    {
      return WEXITSTATUS(child_status); // exit value the child passed to exit()
    }
    else // if child terminated abnormally
    {
      int sigNum = WTERMSIG(child_status);
      printf("terminated by signal %d\n", sigNum);
      fflush(stdout);
      return sigNum; // signal number that caused the child to terminate
    }
  }
}
