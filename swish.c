#define _GNU_SOURCE

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job_list.h"
#include "string_vector.h"
#include "swish_funcs.h"

#define CMD_LEN 512
#define PROMPT "@> "

int main(int argc, char **argv) {
    // Set up shell to ignore SIGTTIN, SIGTTOU when put in background
    // You should adapt this code for use in run_command().
    struct sigaction sac;
    sac.sa_handler = SIG_IGN;
    if (sigfillset(&sac.sa_mask) == -1) {
        perror("sigfillset");
        return 1;
    }

    sac.sa_flags = 0;

    if (sigaction(SIGTTIN, &sac, NULL) == -1 || sigaction(SIGTTOU, &sac, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    strvec_t tokens;
    strvec_init(&tokens);
    job_list_t jobs;
    job_list_init(&jobs);
    char cmd[CMD_LEN];

    printf("%s", PROMPT);
    while (fgets(cmd, CMD_LEN, stdin) != NULL) {
        // Need to remove trailing '\n' from cmd. There are fancier ways.
        int i = 0;
        while (cmd[i] != '\n') {
            i++;
        }
        cmd[i] = '\0';

        if (tokenize(cmd, &tokens) != 0) {
            printf("Failed to parse command\n");
            strvec_clear(&tokens);
            job_list_free(&jobs);
            return 1;
        }

        if (tokens.length == 0) {
            printf("%s", PROMPT);
            continue;
        }

        const char *first_token = strvec_get(&tokens, 0);

        if (strcmp(first_token, "pwd") == 0) {
            // Print the shell's current working directory
            if (getcwd(cmd, sizeof(cmd)) != NULL) {
                printf("%s\n", cmd);
            } else {
                perror("getcwd");
            }
        }

        else if (strcmp(first_token, "cd") == 0) {
            // Change the shell's current working directory
            const char *second_token = strvec_get(&tokens, 1);
            if (strvec_get(&tokens, 1) == NULL) {
                perror("strvec_get");
            }

            if (second_token != NULL) {
                if (chdir(second_token) != 0) {
                    perror("chdir");
                }
            } else {
                char *home = getenv("HOME");
                if (!home) {
                    fprintf(stderr, "HOME environment variable not set\n");
                } else if (chdir(home) != 0) {
                    perror("chdir");
                }
            }
        }

        else if (strcmp(first_token, "exit") == 0) {
            strvec_clear(&tokens);
            break;
        }

        // Print out current list of pending jobs
        else if (strcmp(first_token, "jobs") == 0) {
            int i = 0;
            job_t *current = jobs.head;
            while (current != NULL) {
                char *status_desc;
                if (current->status == BACKGROUND) {
                    status_desc = "background";
                } else {
                    status_desc = "stopped";
                }
                printf("%d: %s (%s)\n", i, current->name, status_desc);
                i++;
                current = current->next;
            }
        }

        // Move stopped job into foreground
        else if (strcmp(first_token, "fg") == 0) {
            if (resume_job(&tokens, &jobs, 1) == -1) {
                printf("Failed to resume job in foreground\n");
            }
        }

        // Move stopped job into background
        else if (strcmp(first_token, "bg") == 0) {
            if (resume_job(&tokens, &jobs, 0) == -1) {
                printf("Failed to resume job in background\n");
            }
        }

        // Wait for a specific job identified by its index in job list
        else if (strcmp(first_token, "wait-for") == 0) {
            if (await_background_job(&tokens, &jobs) == -1) {
                printf("Failed to wait for background job\n");
            }
        }

        // Wait for all background jobs
        else if (strcmp(first_token, "wait-all") == 0) {
            if (await_all_background_jobs(&jobs) == -1) {
                printf("Failed to wait for all background jobs\n");
            }
        }

        else {
            // If the last token input by the user is "&", start the current
            // command in the background.
            int is_background = 0;
            if (strvec_get(&tokens, tokens.length - 1) == NULL) {
                perror("strvec_get");
            }
            if (tokens.length > 0 && strcmp(strvec_get(&tokens, tokens.length - 1), "&") == 0) {
                is_background = 1;
                // Remove "&" from tokens
                strvec_take(&tokens, tokens.length - 1);
            }

            // If the user input does not match any built-in shell command,
            // treat the input as a program name and command-line arguments
            pid_t child_pid = fork();
            if (child_pid == -1) {
                perror("fork");
            } else if (child_pid == 0) {
                // Child Process
                if (is_background) {
                    // Set child process in its own process group
                    if (setpgid(0, 0) == -1) {
                        perror("setpgid");
                    }
                }

                // Ensure the child terminates on failure
                if (run_command(&tokens) == -1) {
                    exit(EXIT_FAILURE);
                }
            } else {
                // Parent Process
                // Ensure the child process runs in its own process group
                if (setpgid(child_pid, child_pid) == -1) {
                    perror("setpgid");
                }

                if (is_background) {
                    // Add background job to job list and do NOT wait for it
                    if (job_list_add(&jobs, child_pid, tokens.data[0], BACKGROUND) == -1) {
                        perror("job_list_add");
                    }
                } else {
                    // Foreground execution
                    // Set the child process as the foreground process group
                    if (tcsetpgrp(STDIN_FILENO, child_pid) == -1) {
                        perror("tcsetpgrp");
                    }

                    // Handle the issue of foreground/background terminal process
                    // groups.
                    // Wait for child to finish
                    int status;
                    if (waitpid(child_pid, &status, WUNTRACED) == -1) {
                        perror("waitpid");
                    }

                    // If the child was stopped, reset the shell as foreground process
                    if (WIFSTOPPED(status)) {
                        if (tcsetpgrp(STDIN_FILENO, getpid()) == -1) {
                            perror("tcsetpgrp");
                        }

                        // Add the stopped job to the job list
                        if (job_list_add(&jobs, child_pid, tokens.data[0], STOPPED) == -1) {
                            perror("job_list_add");
                        }
                    }

                    // Restore the shell itself as the foreground process group
                    if (tcsetpgrp(STDIN_FILENO, getpid()) == -1) {
                        perror("tcsetpgrp");
                    }
                }
            }
        }

        strvec_clear(&tokens);
        printf("%s", PROMPT);
    }

    job_list_free(&jobs);
    return 0;
}
