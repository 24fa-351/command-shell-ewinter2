#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>

#define MAX_INPUT 1024
#define MAX_ARGS 100
#define MAX_ENV_VARS 100

typedef struct {
    char *key;
    char *value;
 } EnvVar;

EnvVar env_vars[MAX_ENV_VARS];
int env_var_count = 0;

void set_env_var(const char *key, const char *value) {
    for(int ix = 0; ix < env_var_count; ix++) {
        if (strcmp(env_vars[ix].key, key)== 0) {
            free(env_vars[ix].value);
            env_vars[ix].value = strdup(value);
            return;
        }
    }
    if (env_var_count < MAX_ENV_VARS) {
        env_vars[env_var_count].key = strdup(key);
        env_vars[env_var_count].value = strdup(value);
        env_var_count++;
    }
}

void unset_env_var(const char *key){
    for (int ix = 0; ix < env_var_count; ix++) {
        if (strcmp(env_vars[ix].key, key) == 0) {
            free(env_vars[ix].key);
            free(env_vars[ix].value);
            env_vars[ix] = env_vars[--env_var_count];
            return;
        }
    }
}

void *get_env_var(const char *key){
    for (int ix = 0; ix < env_var_count; ix++) {
        if (strcmp(env_vars[ix].key, key) == 0) {
            return env_vars[ix].value;
        }
    }
    return NULL;
}

void replace_env_vars(char *input) {
    char *pos;
    while((pos = strchr(input, '$')) != NULL) {
        char *end = pos + 1;
        while (*end && (isalnum(*end) || *end == '_')) {
            end++;
        }

        char var_name[256] ={0};
        strncpy(var_name, pos + 1, end - pos - 1);

        char *value = get_env_var(var_name);
        char temp[MAX_INPUT];

        snprintf(temp, pos - input + 1, "%s", input);
        strcat(temp, value ? value : "");
        strcat(temp, end);

        strcpy(input, temp);
    }
}

void execute_pipe(char ** cmds, int num_cmds) {
    int pipefds[(num_cmds - 1) * 2];

    for (int ix = 0; ix < num_cmds - 1; ix++) {
        if (pipe(pipefds + ix * 2) < 0) {
            perror("Pipe Creation Failed");
            return;
        };
    }

    for (int ix = 0; ix < num_cmds; ix++) {
        pid_t pid = fork();
        if (pid == 0) {
            if(ix > 0) {
                dup2(pipefds[(ix - 1) * 2], STDIN_FILENO);
            }
            if (ix < num_cmds - 1) {
                dup2(pipefds[ix * 2 + 1], STDOUT_FILENO);
            }

            for (int jx = 0; jx < (num_cmds - 1) * 2; jx++) {
                close(pipefds[jx]);
            }

            char *args[MAX_ARGS];
            char *token = strtok(cmds[ix], " ");
            int arg_count = 0;
            while (token != NULL) {
                args[arg_count++] = token;
                token = strtok(NULL, " ");
            }
            args[arg_count] = NULL;

            execvp(args[0], args);
            perror("Command Exec Failed");
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("Fork Failed");
            return;
        }
    }

    for (int ix = 0; ix < 2 * (num_cmds - 1); ix++) {
        close(pipefds[ix]);
    }

    for (int ix = 0; ix < num_cmds; ix++) {
        wait(NULL);
    }
}

void execute_command(char **args, int background, char *input_file, char*output_file) {
    pid_t pid = fork();
    if (pid == 0) {
        if (input_file) {
            int fd = open(input_file, O_RDONLY);
            if (fd == -1) {
                perror("Input File Open Failed");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        if (output_file) {
            int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                perror("Output File Open Failed");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        execvp(args[0], args);
        perror("Command not found");
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("Fork Failed");
    } else {
        if (!background) {
            waitpid(pid, NULL, 0);
        }
    }
}

void parse_and_execute(char *input) {
    char *commands[MAX_ARGS];
    char *cmd; 
    int num_cmds = 0;

    replace_env_vars(input);

    cmd = strtok(input, "|");
    while (cmd != NULL) {
        commands[num_cmds++] = cmd;
        cmd = strtok(NULL, "|");
    }

    if (num_cmds > 1) {
        execute_pipe(commands, num_cmds);
        return;
    }

    char *args[MAX_ARGS];
    char *token = strtok(commands[0], " ");
    int arg_count = 0;
    int background = 0;
    char *input_file = NULL;
    char *output_file = NULL;

    while (token != NULL) {
        if (strcmp(token, "&") == 0) {
            background = 1;
        } else if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " ");
            input_file = token;
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " ");
            output_file = token;
        } else {
            args[arg_count++] = token;
        }
        token = strtok(NULL, " ");
    }
    args[arg_count] = NULL;

    if (arg_count == 0) {
        return;
    }

    if (strcmp(args[0], "exit") == 0 || strcmp(args[0], "quit") == 0) {
        exit(0);
    } else if (strcmp(args[0], "cd") == 0) {
        if (args[1] && chdir(args[1]) != 0) {
            perror("cd failed");
        }
    } else if (strcmp(args[0], "pwd") == 0) {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            perror("pwd failed");
        }
    } else if (strcmp(args[0], "set") == 0 && args[1] && args[2]) {
        set_env_var(args[1], args[2]);
    } else if (strcmp(args[0], "unset") == 0 && args[1]) {
        unset_env_var(args[1]);
    } else {
        execute_command(args, background, input_file, output_file);
    }
}




int main() {
    char input[MAX_INPUT];

    while (1) {
        printf("xsh# ");
        fflush(stdout);

        if(!fgets(input, sizeof(input), stdin)) {
            break;
        }
        input[strcspn(input, "/n")] = 0; // to remove newline

        parse_and_execute(input);
    }

    return 0;
}
