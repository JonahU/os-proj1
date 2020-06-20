#define EXIT_BYE 10
#define EXIT_ON_FAILURE 11 // to distinguish from programs that return 1 upon success

#include <linux/limits.h> // PATH_MAX
#include <fcntl.h> // open()
#include <stdbool.h>
#include <stdlib.h> // exit(), getenv()
#include <string.h>
#include <sys/stat.h>  // S_IRWXU
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h> // getcwd(), chdir(), fork(), execvp(), write()
#include "debug.h"

const char* CMD_CD   = "cd";
const char* CMD_ECHO = "echo";
const char* CMD_PWD  = "pwd";
const char* CMD_QUIT = "bye";

static int exit_status(int status) {
    int res = 0;
    if (WEXITSTATUS(status) == EXIT_ON_FAILURE)
        res = -1;
    else if (WEXITSTATUS(status) == EXIT_BYE)
        exit(EXIT_SUCCESS);
    return res;
}

// REDIRECTION HANDLING
static int redir_file(char const* path) {
    int filedesc = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
    if (filedesc < 0) {
        printf_debug("DEBUG: open(%s) failed\n", path);
        return -1;
    }
    int new_filedesc = dup2(filedesc, STDOUT_FILENO);
    if (new_filedesc < 0) {
        printf_debug("DEBUG: dup2() failed\n");
        return -1;
    }
    return 0;
}

static int redir_pipe(int pipefd[2]) {
    int filedesc = dup2(pipefd[1], STDOUT_FILENO);
    int success = filedesc;
    if (filedesc < 0) {
        printf_debug("DEBUG: dup2() failed\n");
        success = -1;
    }
    success = close(pipefd[1]) == -1 ? -1 : success; // close output copy fd after dup2
    success = close(pipefd[0]) == -1 ? -1 : success; // close input side of pipe
    if (success == -1)
        printf_debug("DEBUG: closing pipe file descriptor failed\n");
    return success;
}

static int setup_redir(char const type, char *const argv[], int pipefd[2]) {
    int res = 0;
    switch (type) {
        case '>':
            res = redir_file(argv[0]);
            break;
        case '|':
            res = redir_pipe(pipefd);
            break;
        case '\0':
            // do nothing
            break;
        default:
            printf_debug("DEBUG: Unknown redir type \"%c\"\n", type);
            res = -1;
            break;
    }
    return res;
}

static int setup_pipe_parent(int pipefd[2], int* restore_stdin, int* restore_stdout) {
    int res = pipe(pipefd);
    if (res == -1) {
        printf_debug("DEBUG: pipe() failed\n");
    } else {
        *restore_stdin = dup(STDIN_FILENO);
        *restore_stdout = dup(STDIN_FILENO);
        if (*restore_stdin == -1 || *restore_stdout == -1) {
            printf_debug("DEBUG: dup() failed\n");
            res = -1;
        }
    }
    return res;
}

static int close_pipe_parent(int pipefd[2]) {
    int res = dup2(pipefd[0], STDIN_FILENO);
    if (res == -1) {
        printf_debug("DEBUG: dup2() failed\n");
        return res;
    }
    res = close(pipefd[0]) == -1 ? -1 : res; // close input copy fd after dup2
    res = close(pipefd[1]) == -1 ? -1 : res; // close output end of pipe
    if (res == -1)
        printf_debug("DEBUG: closing pipe file descriptor failed\n");
    return res;
}

static int restore_stdio(int restore_stdin, int restore_stdout) {
    int restored_stdin = dup2(restore_stdin, STDIN_FILENO);
    int restored_stdout = dup2(restore_stdout, STDOUT_FILENO);
    int success = 0;
    if (restored_stdin == -1 || restored_stdout == -1) {
        printf_debug("DEBUG: dup2() failed\n");
        success = -1;
    }
    return success;
}

// BUILTIN COMMANDS
bool is_builtin(char const* cmd) {
    if (cmd == NULL)
        return false;
    else if (
        strcmp(cmd, CMD_CD)   == 0 ||
        strcmp(cmd, CMD_ECHO) == 0 ||
        strcmp(cmd, CMD_PWD)  == 0 ||
        strcmp(cmd, CMD_QUIT) == 0
    )
        return true;
    else
        return false;
}

static void builtin(char const* cmd, char *const argv[], char const redir_type) {
    int success = 0;
    if (strcmp(cmd, CMD_QUIT) == 0) {
        char* arg1 = argv[1];
        if (arg1 != NULL) {
            printf_debug("DEBUG: \"bye\" failed, >0 args provided\n");
            success = -1;
        } else if (redir_type != '\0') {
            printf_debug("DEBUG: \"bye\" failed, tried to redirect\n");
            success = -1;
        } else {
            success = EXIT_BYE;
        }
    } else if (strcmp(cmd, CMD_ECHO) == 0) {
        write(STDOUT_FILENO, argv[1], strlen(argv[1]));
        write(STDOUT_FILENO, "\n", 1);
    } else if (strcmp(cmd, CMD_PWD) == 0) {
        char* arg1 = argv[1];
        if (arg1 != NULL) {
            printf_debug("DEBUG: \"pwd\" failed, >0 args provided\n");
            success = -1;
        } else {
            char buf[PATH_MAX];
            char* ptr = getcwd(buf, sizeof(buf));
            if (ptr == NULL) {
                printf_debug("DEBUG: getcwd() failed\n");
                success = -1;
            } else {
                write(STDOUT_FILENO, buf, strlen(buf));
                write(STDOUT_FILENO, "\n", 1);
            }
        }
    } else if (strcmp(cmd, CMD_CD) == 0) {
        // calling chdir is useless in child process, call builtin_chdir() in parent
        printf_debug("DEBUG: Invalid call to builtin(\"cd\"), call builtin_chdir() instead\n");
        success = -1;
    } else {
        // not a builtin function
        printf_debug("DEBUG: Unknown builtin command: \"%s\"\n", cmd);
        success = -1;
    }
    if (success == EXIT_BYE)
        exit(EXIT_BYE);
    else if (success < 0)
        exit(EXIT_ON_FAILURE);
    else
        exit(EXIT_SUCCESS);
}

static int builtin_chdir(char const* cmd, char *const argv[]) {
    /* RETURN VALUE
        1  - if cmd is not "cd"
        0  - if "chdir()" succeeded
        -1 - if "chdir()" failed 
    */
    int success = 1;
    if (strcmp(cmd, CMD_CD) == 0) {
        char* arg1 = argv[1];
        char* arg2 = argv[2];
        if (arg2 != NULL) {
            printf_debug("DEBUG: chdir() failed, >1 arg provided\n");
            success = -1;
        } else {
            if (arg1 == NULL)
                success = chdir(getenv("HOME"));
            else
                success = chdir(arg1);
            if (success == -1)
                printf_debug("DEBUG: chdir() failed with arg: \"%s\"\n", arg1);
        }
    }
    return success;
}

int exec_builtin(char const* cmd, char *const argv[], char const redir_type, char *const redir_argv[], int pipefd[2]) {
    // check if cmd is "cd" first
    int chdir_success = builtin_chdir(cmd, argv);
    if (chdir_success <= 0)
        return chdir_success;

    // cmd is not "cd" then continue
    int success = 0;
    pid_t pid = fork();
    if(pid < 0) {
        // fork failed
        printf_debug("DEBUG: fork() failed\n");
        success = -1;
    } else if (pid == 0) {
        // child process
        success = setup_redir(redir_type, redir_argv, pipefd);
        if (success < 0)
            exit(EXIT_ON_FAILURE);
        builtin(cmd, argv, redir_type);
    } else {
        // parent process
        int status = 0;
        wait(&status);
        success = exit_status(status);
        if (success == -1)
            printf_debug("DEBUG: Command failed:\"%s\", arg=%s\n", cmd, argv[1]);
    }
    return success;
}

// EXTERNAL COMMANDS
static void external(char const* cmd, char *const argv[]) {
    execvp(cmd, argv);
    exit(EXIT_ON_FAILURE);
}

int exec_extern(char const* cmd, char *const argv[], char const redir_type, char *const redir_argv[], int pipefd[2]) {
    int success = 0;
    pid_t pid = fork();
    if(pid < 0) {
        // fork failed
        printf_debug("DEBUG: fork() failed\n");
        success = -1;
    } else if (pid == 0) {
        // child process
        success = setup_redir(redir_type, redir_argv, pipefd);
        if (success < 0)
            exit(EXIT_ON_FAILURE);
        external(cmd, argv);
    } else {
        // parent process
        int status = 0;
        wait(&status);
        success = exit_status(status);
        if (success == -1)
            printf_debug("DEBUG: Command failed:\"%s\", arg=%s\n", cmd, argv[1]);
    }
    return success;
}

// BUILTIN AND EXTERNAL COMMANDS
int exec_cmd(char *const argv[], char const redir_type, char *const redir_argv[]) {
    int res = 0;
    char const* cmd = *argv;
    if (cmd == NULL) // empty cmd, do nothing
        return res;

    // setup pipe if necessary
    int pipefd[2];
    int restore_stdin, restore_stdout;
    if(redir_type == '|' && setup_pipe_parent(pipefd, &restore_stdin, &restore_stdout) < 0)
        return -1;

    if (is_builtin(cmd))
        res = exec_builtin(cmd, argv, redir_type, redir_argv, pipefd); 
    else
        res = exec_extern(cmd, argv, redir_type, redir_argv, pipefd);
    if (res < 0)
        return res;

    // pipe to next cmd if necessary
    if(redir_type == '|') {
        res = close_pipe_parent(pipefd);
        if (res < 0)
            return res;

        // exec next cmd
        char const* next_cmd = *redir_argv;
        if (is_builtin(next_cmd))
            res = exec_builtin(next_cmd, redir_argv, '\0', NULL, pipefd); 
        else
            res = exec_extern(next_cmd, redir_argv, '\0', NULL, pipefd);

        // restore file descriptors back to normal
        if(restore_stdio(restore_stdin, restore_stdout) < 0)
            res = -1;
    }
    return res;
}

int exec_cmds_seq(char ***const cmds, size_t len, char const* redir_types, char ***const redir_cmds) {
    int res = 0;
    for(int i=0; i<len; i++) {
        int success = exec_cmd(cmds[i], redir_types[i], redir_cmds[i]);
        if (success < 0)
            res = success;
    }
    return res;
}

int exec_cmds_par(char ***const cmds, size_t len, char const* redir_types, char ***const redir_cmds) {
    int res = 0;
    pid_t pids[len];
    for(int i=0; i<len; i++) {
        char *const cmd = cmds[i][0];

        // check if cmd is "cd" first
        int chdir_success = builtin_chdir(cmd, cmds[i]);
        if (chdir_success <= 0) {
            // this "cd" behaviour in parallel mode is different to a regular shell
            pids[i] = chdir_success;
            continue;
        }

        // cmd is not "cd" then continue
        pids[i] = fork();
        if (pids[i] < 0) {
            // fork failed
            printf_debug("DEBUG: fork() failed\n");
            res = -1;
        } else if(pids[i] == 0) {
            // child
            if (redir_types[i] == '|') {
                int success = exec_cmd(cmds[i], redir_types[i], redir_cmds[i]);
                if (success < 0)
                    exit(EXIT_ON_FAILURE);
                else
                    exit(EXIT_SUCCESS);
            } else {
                int const success = setup_redir(redir_types[i], redir_cmds[i], NULL);
                if (success < 0)
                    exit(EXIT_ON_FAILURE);
                if (is_builtin(cmd)) {
                    builtin(cmd, cmds[i], redir_types[i]);
                } else {
                    external(cmd, cmds[i]);
                }
            }
        }
    }

    int status = 0;
    for(int i=0; i<len; i++) {
        if (pids[i] > 0) {
            wait(&status);
            int success = exit_status(status); // if "bye" was entered shell will exit here, this behaviour is different to a regular shell
            if (success == -1) {
                printf_debug("DEBUG: One or more commands failed\n");
                res = success;
            }
        } 
    }
    return res;
}
