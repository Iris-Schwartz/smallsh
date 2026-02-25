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

void redirect(struct command_line *curr_command);
int background_command(char *argv[]);
int foreground_command(char *argv[]);
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
  int lastExitStatus = 0;

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
      // if (lastSignalNum == 0)
      // {
      //   exit(0);
      // }

      {
        printf("exit value %d\n", lastExitStatus);
      }
    }
    else if (strcmp(curr_command->argv[0], "exit") == 0)
    {
      // kill(-1, 9);
      exit(0);
      printf("Exited");
    }
    else if (strcmp(curr_command->argv[0], "cd") == 0)
    {
      // I asked ChatGPT how to change directories
      // look up where getenv was in the notes
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
      // TA suggested that the second argument of the call to execvp be curr_command->argv
      if (curr_command->is_bg)
      {
        background_command(curr_command->argv);
      }
      else
      {
        lastExitStatus = foreground_command(curr_command->argv);
      }
    }
  }
  return EXIT_SUCCESS;
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
      printf("error redirecting stdout to output file");
      exit(1);
    };
    if (curr_command->is_bg)
    {
      int fd2 = open("/dev/null", O_WRONLY);
      if (dup2(fd2, 0) == -1)
      {
        printf("error redirecting stdout to input file");
        exit(1);
      };
    }
    close(fd1);
    printf("Stdout redirected to output file.\n");
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
      printf("Error redirecting stdin to input file");
      exit(1);
    };
    if (curr_command->is_bg)
    {
      int fd2 = open("/dev/null", O_RDONLY);
      if (dup2(fd2, 1) == -1)
      {
        printf("error redirecting stdout to output file");
        exit(1);
      };
    }
    close(fd2);
    printf("stdin redirected to input file.\n");
  }
}

// void handle_SIGTSTP(int signal)
// {
// }

int background_command(char *argv[])
{
  int childStatus;

  pid_t idOfChild = fork();

  switch (idOfChild)
  {
  case 0:
    redirect(argv[0]);
    if (execvp(argv[0], argv) == -1)
    {
      perror("Child process could not execute new program");
      exit(1);
    }
    break;
  default:
    idOfChild = waitpid(idOfChild, &childStatus, WNOHANG);
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

int foreground_command(char *argv[])
{
  int childStatus;

  pid_t idOfChild = fork();

  switch (idOfChild)
  {
  case -1:
    perror("issue with fork()");
    exit(1);
  case 0:
    redirect(argv[0]);
    if (execvp(argv[0], argv) == -1)
    {
      printf("%s: no such file or directory\n", argv[0]);
      exit(1);
    }
    break;
  default:
    idOfChild = waitpid(idOfChild, &childStatus, 0);
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
