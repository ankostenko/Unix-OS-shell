#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

/* 
   Definition of struct tokens. We need repeat it because using 'struct tokens *tokens'
   referencing to header's 'struct tokens' so that causes error of incomplete type
 */
struct tokens{
    size_t tokens_length;
    char **tokens;
    size_t buffers_length;
    char **buffers;
};

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);

char *procpathenv(char *, char *);
char *detpath(char *);
char **args_proc(char *, struct tokens *);

int redirection(char *, int);

const int MAX_PATH_SIZE = 128;

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
    cmd_fun_t *fun;
    char *cmd;
    char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
    {cmd_help, "?", "show this help menu"},
    {cmd_exit, "exit", "exit the command shell"},
    {cmd_pwd, "pwd", "prints the current working directory"},
    {cmd_cd, "cd", "changes current working directory on directory provided by arg"},
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
    for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
        printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
    return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) {
    exit(0);
}

/* Prints current working directory */
int cmd_pwd(unused struct tokens *tokens){
    char *path_buff = NULL;
    path_buff = (char *)malloc(sizeof(char) * MAX_PATH_SIZE);
    if (!path_buff) 
        return -1;
    /* Copy absolute path of working directory to path_buff */
    getcwd(path_buff, MAX_PATH_SIZE);
    fprintf(stdout, "Path to current directory: %s\n", path_buff);
    free(path_buff);
    return 1;
}

/* Changes current working directory */
int cmd_cd(struct tokens *tokens){
    if(chdir(tokens->tokens[1]) == -1){
        printf("No such file or directory\n");
    }
    return 1; 
}

/* Execute program */                                                                                
/*
    So what have I found?
    FIXME: program corrupts it own files
    FIXME: '>' creates executable files  
    FIXME: '<' not even works 
    FIXME: core fault 
*/
int shell_exec(struct tokens *tokens){
    /* path processed by detpath and then we could use it */ 
    char *path = detpath(tokens_get_token(tokens, 0));
    char **args = args_proc(path, tokens);    

    pid_t cpid;
    int status;	
    cpid = fork();
    /* cpid > 0 - parent process, cpid == 0 - child process, cpid < 0 - error */
    if (cpid > 0) { 
        wait(&status);
    } else if (cpid == 0){
        /* executes program according to path and given arguments */
        execv(path, args);
        exit(0);
    } else {
        /* cannot fork current process */
        exit(1);
    }
    return 1;
}

/* processes arguments to determine what is this redirection or merely arguments passing */
char** args_proc(char *path, struct tokens *tokens){
    int i;
    char **args = (char **)malloc((tokens->tokens_length + 1) * sizeof(char *));
    if (!args) exit(1);
    
    args[0] = path;
    for (i = 1; i < tokens->tokens_length; i++)
        args[i] = tokens_get_token(tokens, i);
    args[i] = NULL;

    /* is it redirection? */
    if (args[1] != NULL && (!strcmp(args[1], ">") || !strcmp(args[1], "<"))){
        if (!strcmp(args[1], ">")){
            /* this is not really good solution, because it doesn't allow a pipe */
            redirection(args[2], 1);
        }else{
            redirection(args[2], 0);
        }
        args[1] = NULL;
    }

    return args;
}

/* redirect stdin or stdout depends on stream */
int redirection(char *path, int stream){
    int fd;

    fd = open(path, O_NONBLOCK); 
    if (fd == -1) return -1;

    if (dup2(fd, stream) < 0) return -1;

    return 0; 
}

/*
    Shell can use both absolute path and relative path.
    1. Determine whether is there the symbol '/'.
    2. If it does then path is absolute, so we just use it.
    3. If it doesn't then either it is a relative path and we use it or it is name of the program.
    4. If it's a name so we lookup in the current working directory.
    5. If it absent we lookup on the PATH environment variables.
 */
char* detpath(char *ppath){
    /* ppath is absolute path  */
    if (*ppath == '/')
        return ppath;

    /* Maybe dir would be opened and not closed. */
    if (!opendir(ppath)){
        /* A component of ppath is not a directory so it's a name of the program */
        if (errno == ENOENT){
            /* enpath - path from PATH  environment variable concatenated w/ name of the program */
            char *enpath = procpathenv(getenv("PATH"), ppath);
            if (!enpath) return NULL;
            return enpath; 
        }
    }

    /* ppath is relative path */
    return ppath;
}

/* process PATH environment variables */
char* procpathenv(char* env, char *name){
    static char *path = NULL;
    path = (char *)malloc(sizeof(char) * MAX_PATH_SIZE);
    if (!path) return NULL;
    struct stat statbuf;
    int i = 0;
    for (char *c = env; *c != '\000'; c++, i++){
        if (*c == ':'){
            if (*(path + i - 1) != '/')
                *(path + i) = '/';
            *(path + i + 1) = '\000';
            if (!stat(strcat(path, name), &statbuf)){
                break;
            }

            i = -1;
            continue;
        }
        *(path + i) = *c;
    }
    
    return path;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
    for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
        if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
            return i;
    return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
    /* Our shell is connected to standard input. */
    shell_terminal = STDIN_FILENO;

    /* Check if we are running interactively */
    shell_is_interactive = isatty(shell_terminal);

    if (shell_is_interactive) {
        /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
         * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
         * foreground, we'll receive a SIGCONT. */
        while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);

       /* Saves the shell's process id */
        shell_pgid = getpid();

        /* Take control of the terminal */
        tcsetpgrp(shell_terminal, shell_pgid);

        /* Save the current termios to a variable, so it can be restored later. */
        tcgetattr(shell_terminal, &shell_tmodes);
    }
}

int main(unused int argc, unused char *argv[]) {
    init_shell();

    static char line[4096];
    int line_num = 0;

    /* Please only print shell prompts when standard input is not a tty */
    if (shell_is_interactive)
        fprintf(stdout, "%d: ", line_num);

    while (fgets(line, 4096, stdin)) {
        /* Split our line into words. */
        struct tokens *tokens = tokenize(line);

        /* Find which built-in function to run. */
        int fundex = lookup(tokens_get_token(tokens, 0));

        if (fundex >= 0) {
            cmd_table[fundex].fun(tokens);
        } else {
            shell_exec(tokens);  
        }

        if (shell_is_interactive)
            /* Please only print shell prompts when standard input is not a tty */
            fprintf(stdout, "%d: ", ++line_num);

        /* Clean up memory */
        tokens_destroy(tokens);
    }

    return 0;
}
