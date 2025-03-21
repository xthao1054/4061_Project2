#define _GNU_SOURCE

#include "swish_funcs.h"

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job_list.h"
#include "string_vector.h"

#define MAX_ARGS 10

// Tokenize string s
int tokenize(char *s, strvec_t *tokens) {
    //  Assume each token is separated by a single space (" ")
    //  Use the strtok() function to accomplish this
    char *word = strtok(s, " ");

    // Check if there are any tokens found
    if (word == NULL) {
        perror("input string is empty");
        return -1;
    }

    // Add each token to the 'tokens' parameter (a string vector)
    while (word != NULL) {
        if (strvec_add(tokens, word) == -1) {
            perror("failure to tokenize: strvec_add");
            // Return -1 on error
            return -1;
        }

        word = strtok(NULL, " ");
    }

    // Return 0 on success
    return 0;
}

// Execute the specified program (token 0) with the specified command-line arguments and perform
// output redirection before exec()'ing THIS FUNCTION SHOULD BE CALLED FROM A CHILD OF THE MAIN
// SHELL PROCESS
int run_command(strvec_t *tokens) {
    // No command entered, return error
    if (tokens->length == 0) {
        return -1;
    }

    // Build a string array from the 'tokens' vector and pass this into execvp()
    char *args[MAX_ARGS];
    int input_fd = -1;
    int output_fd = -1;

    // Check for '<' (redirect input), '>' (redirect output), '>>' (redirect and append output)
    // entries inside of 'tokens' (the strvec_find() function will do this for you)
    int in_index = strvec_find(tokens, "<");
    int out_index = strvec_find(tokens, ">");
    int append_index = strvec_find(tokens, ">>");

    // Handle input redirection
    if (in_index != -1) {
        // Open the necessary file for reading (<), writing (>), or appending (>>)
        input_fd = open(tokens->data[in_index + 1], O_RDONLY);

        if (input_fd == -1) {
            perror("Failed to open input file");
            return -1;
        }

        if (input_fd != -1) {
            // Use dup2() to redirect stdin (<), stdout (> or >>)
            if (dup2(input_fd, STDIN_FILENO) == -1) {
                perror("dup2 input redirection failed");
                close(input_fd);
                return -1;
            }
        }

        if (close(input_fd) == -1) {
            perror("Failed to close input file");
        }
    }

    // Handle output redirection (overwrite)
    if (out_index != -1) {
        // Open the necessary file for reading (<), writing (>), or appending (>>)
        output_fd =
            open(tokens->data[out_index + 1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

        if (output_fd == -1) {
            perror("Failed to open output file");
            return -1;
        }

        // Use dup2() to redirect stdin (<), stdout (> or >>)
        if (dup2(output_fd, STDOUT_FILENO) == -1) {
            perror("dup2 output redirection failed");
            close(output_fd);
            return -1;
        }

        if (close(output_fd) == -1) {
            perror("Failed to output input file");
        }
    }

    // Handle output redirection (append)
    if (append_index != -1) {
        // Open the necessary file for reading (<), writing (>), or appending (>>)
        output_fd =
            open(tokens->data[append_index + 1], O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);

        if (output_fd == -1) {
            perror("Failed to open output file");
            return -1;
        }

        // Use dup2() to redirect stdin (<), stdout (> or >>)
        if (dup2(output_fd, STDOUT_FILENO) == -1) {
            perror("dup2 output redirection failed");
            close(output_fd);
            return -1;
        }

        if (close(output_fd) == -1) {
            perror("Failed to output input file");
        }
    }

    // Restore the signal handlers for SIGTTOU and SIGTTIN to their defaults.
    // The code in main() within swish.c sets these handlers to the SIG_IGN value.
    // Adapt this code to use sigaction() to set the handlers to the SIG_DFL value.
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTTOU, &sa, NULL);
    sigaction(SIGTTIN, &sa, NULL);

    // Change the process group of this process (a child of the main shell).
    // Call getpid() to get its process ID then
    pid_t pid = getpid();
    // call setpgid() and use this processID as the value for the new process group ID
    setpgid(pid, pid);

    // Build argument list, skipping redirection operators and filenames
    int arg_count = 0;
    for (int i = 0; i < tokens->length; i++) {
        if (i == in_index || i == out_index || i == append_index) {
            i++;
        } else {
            if (strvec_get(tokens, i) == NULL) {
                perror("Build argument list: strvec_get");
                return -1;
            }
            args[arg_count++] = strvec_get(tokens, i);
        }
    }

    // Null-terminate the argument list
    args[arg_count] = NULL;

    // Execute the command
    execvp(args[0], args);

    // If execvp() fails, print an error message
    perror("exec");
    exit(EXIT_FAILURE);

    return 0;
}

// Implement the ability to resume stopped jobs in the foreground and in the background
int resume_job(strvec_t *tokens, job_list_t *jobs, int is_foreground) {
    // Look up the relevant job information (in a job_t) from the jobs list
    // using the index supplied by the user (in tokens index 1)
    // Feel free to use sscanf() or atoi() to convert this string to an int
    int job_index = atoi(tokens->data[1]);
    job_t *job = job_list_get(jobs, job_index);
    if (!job) {
        fprintf(stderr, "Job index out of bounds\n");
        return -1;
    }

    pid_t job_pid = job->pid;
    if (!is_foreground) {
        // modify the 'status' field of the relevant job list entry to BACKGROUND
        job->status = BACKGROUND;
    }

    // Call tcsetpgrp(STDIN_FILENO, <job_pid>) where job_pid is the job's process ID
    if (tcsetpgrp(STDIN_FILENO, job_pid) == -1) {
        perror("tcsetpgrp");
        return -1;
    }

    // Send the process the SIGCONT signal with the kill() system call
    if (kill(-job_pid, SIGCONT) < 0) {
        perror("kill");
        return -1;
    }

    // Wait for the job if running in the foreground
    if (is_foreground) {
        // Use the same waitpid() logic as in main -- don't forget WUNTRACED
        if (tcsetpgrp(STDIN_FILENO, job_pid) == -1) {
            perror("tcsetpgrp");
            return -1;
        }

        int status;
        if (waitpid(job_pid, &status, WUNTRACED) == -1) {
            perror("waitpid");
            return -1;
        }

        // If the job has terminated (not stopped), remove it from the 'jobs' list
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (job_list_remove(jobs, job_index) == -1) {
                perror("job_list_remove");
                return -1;
            }
        }

        // Return terminal control to the shell
        if (tcsetpgrp(STDIN_FILENO, getpid()) == -1) {
            perror("tcsetpgrp");
            return -1;
        }
    }

    // Return terminal control to the shell
    if (tcsetpgrp(STDIN_FILENO, getpid()) == -1) {
        perror("tcsetpgrp");
        return -1;
    }

    return 0;
}

// Wait for a specific job to stop or terminate
int await_background_job(strvec_t *tokens, job_list_t *jobs) {
    // Look up the relevant job information (in a job_t) from the jobs list
    //    using the index supplied by the user (in tokens index 1)
    if (tokens->length < 2) {
        fprintf(stderr, "wait-for: Missing job index\n");
        return -1;
    }

    // Convert job index from string to integer
    int job_index = atoi(tokens->data[1]);
    job_t *job = job_list_get(jobs, job_index);
    if (!job) {
        fprintf(stderr, "Job index out of bounds\n");
        return -1;
    }

    // Make sure the job's status is BACKGROUND (no sense waiting for a stopped job)
    if (job->status != BACKGROUND) {
        fprintf(stderr, "Job index is for stopped process not background process\n");
        return -1;
    }

    // Use waitpid() to wait for the job to terminate, as you have in resume_job() and
    // main().
    int status;
    if (waitpid(job->pid, &status, WUNTRACED) == -1) {
        perror("waitpid");
        return -1;
    }

    // If the process terminates (is not stopped by a signal) remove it from the jobs
    // list
    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        if (job_list_remove(jobs, job_index) == -1) {
            fprintf(stderr, "Failed to remove job from list\n");
            return -1;
        }
    }

    return 0;
}

// Wait for all background jobs to stop or terminate
int await_all_background_jobs(job_list_t *jobs) {
    // Iterate through the jobs list, ignoring any stopped jobs
    job_t *current = jobs->head;
    while (current != NULL) {
        // For a background job, call waitpid() with WUNTRACED.
        if (current->status == BACKGROUND) {
            int status;
            if (waitpid(current->pid, &status, WUNTRACED) == -1) {
                perror("waitpid");
                return -1;
            }

            // If the job has stopped, change its status to STOPPED
            // If the job has terminated, do nothing until the
            // next step (don't attempt to remove it while iterating through the list).
            if (WIFSTOPPED(status)) {
                current->status = STOPPED;
            }
        }
        current = current->next;
    }

    // Remove all background jobs (which have all just terminated) from jobs list.
    job_list_remove_by_status(jobs, BACKGROUND);

    return 0;
}
