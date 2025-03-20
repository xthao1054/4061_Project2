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

int tokenize(char *s, strvec_t *tokens) {
    // Task 0: Tokenize string s
    //  Assume each token is separated by a single space (" ")
    //  Use the strtok() function to accomplish this
    //  Return 0 on success, -1 on error
    char *word = strtok(s, " ");

    // Check if string is empty
    if (word == NULL) {
        fprintf(stderr, "input string is empty");
        return -1;
    }

    // Add each token to the 'tokens' parameter (a string vector)
    while (word != NULL) {
        if (strvec_add(tokens, word) == -1) {
            perror("failure to tokenize");
            return -1;
        }

        word = strtok(NULL, " ");
    }

    return 0;
}

int run_command(strvec_t *tokens) {
    // TODO Task 2: Execute the specified program (token 0) with the
    // specified command-line arguments
    // THIS FUNCTION SHOULD BE CALLED FROM A CHILD OF THE MAIN SHELL PROCESS
    // Hint: Build a string array from the 'tokens' vector and pass this into execvp()
    // Another Hint: You have a guarantee of the longest possible needed array, so you
    // won't have to use malloc.

    // No command entered, return error
    if (tokens->length == 0) {
        return -1;
    }

    // Build the argument array for execvp()
    char *args[MAX_ARGS];

    // TODO Task 3: Extend this function to perform output redirection before exec()'ing
    // Check for '<' (redirect input), '>' (redirect output), '>>' (redirect and append output)
    // entries inside of 'tokens' (the strvec_find() function will do this for you)
    // Open the necessary file for reading (<), writing (>), or appending (>>)
    // Use dup2() to redirect stdin (<), stdout (> or >>)
    // DO NOT pass redirection operators and file names to exec()'d program
    // E.g., "ls -l > out.txt" should be exec()'d with strings "ls", "-l", NULL
    int input_fd = -1;
    int output_fd = -1;

    int in_index = strvec_find(tokens, "<");
    int out_index = strvec_find(tokens, ">");
    int append_index = strvec_find(tokens, ">>");

    // Handle input redirection
    if (in_index != -1) {
        input_fd = open(tokens->data[in_index + 1], O_RDONLY);
        if (input_fd == -1) {
            perror("Failed to open input file");
            return -1;
        }
        if (input_fd != -1) {
            if (dup2(input_fd, STDIN_FILENO) == -1) {
                perror("dup2 input redirection failed");
                return -1;
            }
        }
        close(input_fd);
    }

    // Handle output redirection (overwrite)
    if (out_index != -1) {
        output_fd =
            open(tokens->data[out_index + 1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if (output_fd == -1) {
            perror("Failed to open output file");
            return -1;
        }
        if (dup2(output_fd, STDOUT_FILENO) == -1) {
            perror("dup2 output redirection failed");
            return -1;
        }
        close(output_fd);
    }

    // Handle output redirection (append)
    if (append_index != -1) {
        output_fd =
            open(tokens->data[append_index + 1], O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
        if (output_fd == -1) {
            perror("Failed to open output file");
            return -1;
        }
        if (dup2(output_fd, STDOUT_FILENO) == -1) {
            perror("dup2 output redirection failed");
            return -1;
        }
        close(output_fd);
    }

    // TODO Task 4: You need to do two items of setup before exec()'ing
    // 1. Restore the signal handlers for SIGTTOU and SIGTTIN to their defaults.
    // The code in main() within swish.c sets these handlers to the SIG_IGN value.
    // Adapt this code to use sigaction() to set the handlers to the SIG_DFL value.
    // 2. Change the process group of this process (a child of the main shell).
    // Call getpid() to get its process ID then call setpgid() and use this process
    // ID as the value for the new process group ID

    struct sigaction sa;
    sa.sa_handler = SIG_DFL;    // Default signal handling
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTTOU, &sa, NULL);
    sigaction(SIGTTIN, &sa, NULL);

    // Step 2: Set the child process to its own process group
    pid_t pid = getpid();
    setpgid(pid, pid);    // Set the process group ID to its own PID

    // Build argument list, skipping redirection operators and filenames
    int arg_count = 0;
    for (int i = 0; i < tokens->length; i++) {
        if (i == in_index || i == out_index || i == append_index) {
            i++;    // Skip the file name after redirection operators
        } else {
            args[arg_count++] = strvec_get(tokens, i);
        }
    }

    // Null-terminate the argument list
    args[arg_count] = NULL;

    // Execute the command
    execvp(args[0], args);

    // If execvp() fails, print an error message and return -1
    perror("exec");
    exit(EXIT_FAILURE);

    // Should never be reached
    return 0;
}

int resume_job(strvec_t *tokens, job_list_t *jobs, int is_foreground) {
    // TODO Task 5: Implement the ability to resume stopped jobs in the foreground

    // 3. Send the process the SIGCONT signal with the kill() system call
    // 4. Use the same waitpid() logic as in main -- don't forget WUNTRACED
    // 5. If the job has terminated (not stopped), remove it from the 'jobs' list
    // 6. Call tcsetpgrp(STDIN_FILENO, <shell_pid>). shell_pid is the *current*
    //    process's pid, since we call this function from the main shell process

    // 1. Look up the relevant job information (in a job_t) from the jobs list
    // using the index supplied by the user (in tokens index 1)
    // Feel free to use sscanf() or atoi() to convert this string to an int
    int job_index = atoi(tokens->data[1]);
    job_t *job = job_list_get(jobs, job_index);
    if (!job) {
        fprintf(stderr, "Job index out of bounds\n");
        return -1;
    }
    // TODO Task 6: Implement the ability to resume stopped jobs in the background.
    // This really just means omitting some of the steps used to resume a job in the
    // foreground:
    // 1. DO NOT call tcsetpgrp() to manipulate foreground/background terminal process group
    // 2. DO NOT call waitpid() to wait on the job
    // 3. Make sure to modify the 'status' field of the relevant job list entry to
    // BACKGROUND
    //    (as it was STOPPED before this)
    pid_t job_pid = job->pid;
    if (!is_foreground) {
        job->status = BACKGROUND;
    }

    // 2. Call tcsetpgrp(STDIN_FILENO, <job_pid>) where job_pid is the job's process ID
    tcsetpgrp(STDIN_FILENO, job_pid);

    // Step 3: Send SIGCONT to resume the job
    if (kill(-job_pid, SIGCONT) < 0) {
        perror("kill");
        return -1;
    }

    // Step 4: Wait for the job if running in the foreground
    if (is_foreground) {
        tcsetpgrp(STDIN_FILENO, job_pid);
        int status;
        waitpid(job_pid, &status, WUNTRACED);

        // Step 5: Remove job if it has exited
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            job_list_remove(jobs, job_index);
        }
        tcsetpgrp(STDIN_FILENO, getpid());
    } 
    // Step 6: Return terminal control to the shell
    tcsetpgrp(STDIN_FILENO, getpid());

    return 0;
}

int await_background_job(strvec_t *tokens, job_list_t *jobs) {
    // TODO Task 6: Wait for a specific job to stop or terminate
    // 1. Look up the relevant job information (in a job_t) from the jobs list
    //    using the index supplied by the user (in tokens index 1)
    // 2. Make sure the job's status is BACKGROUND (no sense waiting for a stopped job)
    // 3. Use waitpid() to wait for the job to terminate, as you have in resume_job() and
    // main().
    // 4. If the process terminates (is not stopped by a signal) remove it from the jobs
    // list
    if (tokens->length < 2) {
        fprintf(stderr, "wait-for: Missing job index\n");
        return -1;
    }

    int job_index = atoi(tokens->data[1]);
    job_t *job = job_list_get(jobs, job_index);
    if (!job) {
        fprintf(stderr, "Job index out of bounds\n");
        return -1;
    }

    if (job->status != BACKGROUND) {
        fprintf(stderr, "Job index is for stopped process not background process\n");
        return -1;
    }

    int status;
    waitpid(job->pid, &status, WUNTRACED);

    // Remove completed job from the list
    job_list_remove_by_status(jobs, BACKGROUND);

    return 0;
}

int await_all_background_jobs(job_list_t *jobs) {
    // TODO Task 6: Wait for all background jobs to stop or terminate
    // 1. Iterate through the jobs list, ignoring any stopped jobs
    // 2. For a background job, call waitpid() with WUNTRACED.
    // 3. If the job has stopped (check with WIFSTOPPED), change its
    //    status to STOPPED. If the job has terminated, do nothing until the
    //    next step (don't attempt to remove it while iterating through the list).
    // 4. Remove all background jobs (which have all just terminated) from jobs list.
    //    Use the job_list_remove_by_status() function.

    // Wait for all background jobs to terminate
    job_t *current = jobs->head;
    while (current != NULL) {
        if (current->status == BACKGROUND) {
            int status;
            waitpid(current->pid, &status, WUNTRACED);
        }
        current = current->next;
    }

    // Remove all completed background jobs
    job_list_remove_by_status(jobs, BACKGROUND);

    return 0;
}
