#include "execute.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>

// Helper function to search for and execute a command in specified directories.
int execute_with_search(char *const argv[]) {
    const char *directories[] = {"/usr/local/bin", "/usr/bin", "/bin"};
    char path[1024];

    for (int i = 0; i < sizeof(directories) / sizeof(char *); i++) {
        snprintf(path, sizeof(path), "%s/%s", directories[i], argv[0]);
        // Try to execute the command.
        execv(path, argv);
        // If execv returns, it means either the file doesn't exist in the directory,
        // or it's not executable. So, try the next directory.
    }
    // If the command wasn't found in the specified directories, return 0.
    return 0;
}

void execute_command(Command *cmd) {
    int pipe_fds[2]; // File descriptors for the pipe
    pid_t pid1, pid2;
    int fd_in = -1, fd_out = -1; // File descriptors for redirection

    // Setup input redirection if specified
    if (cmd->inputFile) {
        fd_in = open(cmd->inputFile, O_RDONLY);
        if (fd_in < 0) {
            perror("Failed to open input file");
            return;
        }
    }

    // Setup output redirection if not a pipeline; handled later for pipelines
    if (cmd->outputFile && !cmd->isPipe) {
        fd_out = open(cmd->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out < 0) {
            perror("Failed to open output file");
            if (fd_in != -1) close(fd_in);
            return;
        }
    }

    // Setup pipe if necessary
    if (cmd->isPipe) {
        if (pipe(pipe_fds) < 0) {
            perror("Failed to create pipe");
            if (fd_in != -1) close(fd_in);
            return;
        }
    }

    // Execute the first command or the only command
    pid1 = fork();
    if (pid1 == 0) { // Child process
        if (fd_in != -1) {
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }
        if (cmd->isPipe) {
            dup2(pipe_fds[1], STDOUT_FILENO); // Redirect STDOUT to the pipe
        } else if (fd_out != -1) {
            dup2(fd_out, STDOUT_FILENO); // Redirect STDOUT to the output file
            close(fd_out);
        }
        if (!cmd->isPipe) {
            close(pipe_fds[0]);
            close(pipe_fds[1]);
        }

        execvp(cmd->argv[0], cmd->argv); // Execute the command
        perror("execvp failed");
        exit(EXIT_FAILURE);
    }

    // If there's a pipeline, setup and execute the second command
    if (cmd->isPipe) {
        pid2 = fork();
        if (pid2 == 0) { // Child process for the second part of the pipeline
            close(pipe_fds[1]); // Close the write end of the pipe in the second child
            dup2(pipe_fds[0], STDIN_FILENO); // Read from pipe

            if (cmd->outputFile) { // Setup output redirection if specified
                fd_out = open(cmd->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd_out < 0) {
                    perror("Failed to open output file");
                    exit(EXIT_FAILURE);
                }
                dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
            }

            execvp(cmd->pipeCommand[0], cmd->pipeCommand); // Execute the second command
            perror("execvp failed");
            exit(EXIT_FAILURE);
        }
        close(pipe_fds[0]);
        close(pipe_fds[1]);
    }

    // Parent process - wait for child processes to finish
    if (pid1 > 0) waitpid(pid1, NULL, 0);
    if (cmd->isPipe && pid2 > 0) waitpid(pid2, NULL, 0);

    // Cleanup
    if (fd_in != -1) close(fd_in);
    if (fd_out != -1 && !cmd->isPipe) close(fd_out); // Close fd_out if not used for a pipeline
}