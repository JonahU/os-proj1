#define MAX_LEN 66 // 64 chars + newline + null

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h> // STDERR_FILENO
#include "debug.h"
#include "exec.h"
#include "strquote.h"

const char* PROMPT  = "520shell> ";
const char* ERROR   = "An ERROR has occurred\n";

bool is_last_arg(char const* str) {
    if(str[strlen(str)-1] == '\n')
        return true;
    return false;
}

void rm_newline(char* str) {
    if(is_last_arg(str)) {
        str[strlen(str)] = '\0';
        str[strlen(str)-1] = '\0';
    }
}

void append_cmd(char* dest, char const* cmd, unsigned* offset) {
    if(is_last_arg(cmd)) {
        char copy[strlen(cmd)+1];
        strncpy(copy, cmd, strlen(cmd)+1);
        rm_newline(copy);
        if (is_str_lit(copy)) {
            char const* quotes_removed = strip_quotes(copy);
            memcpy(dest + *offset, quotes_removed, strlen(quotes_removed) + 1);
            *offset += strlen(quotes_removed) + 1;
        } else {
            memcpy(dest + *offset, copy, strlen(copy) + 1);
            *offset += strlen(copy) + 1;
        }
    } else if (is_str_lit(cmd)) {
        char copy[strlen(cmd)+1];
        strncpy(copy, cmd, strlen(cmd)+1);
        char const* quotes_removed = strip_quotes(copy);
        memcpy(dest + *offset, quotes_removed, strlen(quotes_removed) + 1);
        *offset += strlen(quotes_removed) + 1;
    } else {
        memcpy(dest + *offset, cmd, strlen(cmd) + 1);
        *offset += strlen(cmd) + 1;
    }
}

int format_cmd(char* src, char* dest) {
    // get cmd
    unsigned offset = 0;
    char const spaces_and_tabs[] = {' ', '\t', '\0'};
    char* token = strtok2(src, spaces_and_tabs);
    if (token == NULL)
        append_cmd(dest, "", &offset);
    else 
        append_cmd(dest, token, &offset);

    // get args (if exists)
    while(token != NULL) {
        token = strtok2(NULL, spaces_and_tabs);
        if(token == NULL)
            *(dest+offset) = '\0';
        else
            append_cmd(dest, token, &offset);
    }
    return 0;
}

char split_redir(char* src, char* dest_redir) {
    char delim = 0;
    char* file_redir = strchr2(src, '>');
    char* pipe_redir = strchr2(src, '|');
    if (file_redir != NULL && pipe_redir != NULL) {
        printf_debug("DEBUG: Cannot mix '>' and '|'\n");
        delim = -1;
    } else  if (
        (file_redir != NULL && strchr2(file_redir+1, '>') != NULL) ||
        (pipe_redir != NULL && strchr2(pipe_redir+1, '|') != NULL)
    ) {
        printf_debug("DEBUG: Cannot redirect more than once\n");
        delim = -1;
    } else if (file_redir != NULL || pipe_redir != NULL) {
        char* delim_str = file_redir != NULL ? ">" : "|";
        char* token = strtok2(src, delim_str); // get the cmd + args str
        size_t cmd_len = strlen(token); // get the length of the cmd + args
        token = strtok2(NULL, delim_str); // get the redirection str

        if (token == NULL || !strcmp(token, " ") || !strcmp(token, "\t") || !strcmp(token, "\n")) { // no redir info
            printf_debug("DEBUG: No redirection destination provided\n");
            delim = -1;
        } else {
            // copy redirection info into dest_redir
            unsigned offset = 0;
            append_cmd(dest_redir, token, &offset);
            *(dest_redir+offset) = '\0';

            // remove redirect info from src
            char* cmd_end = src + cmd_len + 1;
            memset(cmd_end, '\0', strlen(token));
            delim = file_redir != NULL ? '>' : '|';
        }
    }
    return delim;
}

size_t split(char* src, char const* delim, char* dest) {
    unsigned offset = 0;
    char* token = strtok2(src, delim);
    size_t count = 1;
    append_cmd(dest, token, &offset);
    while(token != NULL) {
        token = strtok2(NULL, delim);
        if(token == NULL)
            *(dest+offset) = '\0';
        else {
            ++count;
            append_cmd(dest, token, &offset);
        }
    }
    return count;
}

int buf_to_strs(char* buf, char** dest) {
    char* ptr = buf;
    int i = 0;
    while(*ptr != '\0') {
        dest[i] = ptr;
        ++i;
        ptr += strlen(ptr) + 1;
    }
    dest[i] = NULL;
    return 0;
}

void flush_input_src(FILE* input_src) {
    int ch;
    do {
        ch = fgetc(input_src);
        if(input_src != stdin) // print rest of invalid command in batch mode
            write(STDOUT_FILENO, &ch, 1);
    } while (ch != '\n' && ch != EOF);
}

void log_error(void) {
    write(STDERR_FILENO, ERROR, strlen(ERROR));
}

int main(int argc, char** argv) {
    int exit_code = 0;
    FILE* input_src = stdin;
    if (argc > 2) {
        printf_debug("DEBUG: Too many cmd line args\n");
        log_error();
        exit_code = 1;
    } else if (argc == 2) {
        // batch mode
        input_src = fopen(argv[1], "r");
        if (input_src == NULL) {
            printf_debug("DEBUG: Could not open file \"%s\"\n", argv[1]);
            log_error();
            exit_code = 1;
        }
    }
    if (exit_code == 0)
        setbuf(input_src, NULL);

    // start mysh main loop
    while (exit_code == 0 && !feof(input_src)) {
        // print prompt only in basic shell mode
        if (input_src == stdin)
            write(STDOUT_FILENO, PROMPT, strlen(PROMPT));
        char input_buf[MAX_LEN] = {0};
        char* input = fgets(input_buf, sizeof input_buf, input_src);
        int* next_ch_ptr = NULL;
        int next_ch = 0;
        if (input == NULL) {
            next_ch = fgetc(input_src);
            next_ch_ptr = &next_ch;
            if (next_ch != EOF) {
                printf_debug("DEBUG: fgets() failed\n"); // likely a file descriptor issue
                log_error();
                exit_code = 1;
            }
            break; // end the program (otherwise infinite loop)
        }
        // print cmd if in batch mode
        if (input_src != stdin)
            write(STDOUT_FILENO, input_buf, strlen(input_buf));
        // check if input is > 64 chars or EOF
        if (input_buf[strlen(input_buf)-1] != '\n') { // could check other newline chars also
            if(next_ch_ptr == NULL) { // get next ch from input src if necessary
                next_ch = fgetc(input_src);
                next_ch_ptr = &next_ch;
            }
            if (next_ch == EOF) {
                write(STDOUT_FILENO, "\n", 1); // print missing newline
            } else {
                write(STDOUT_FILENO, next_ch_ptr, 1); // print char used to test EOF or else it will be lost
                flush_input_src(input_src);
                printf_debug("DEBUG: Input >64 characters\n");
                log_error();
                continue;
            }
        }

        int quotes = contains_quotes(input_buf);
        if (quotes < 0 || contains_valid_quotes(input_buf) < 0) {
            log_error();
            continue;
        }

        char* seq_mode = strchr2(input_buf, ';');
        char* par_mode = strchr2(input_buf, '&');
        if (seq_mode != NULL && par_mode != NULL) {
            printf_debug("DEBUG: Cannot mix '&' and ';'\n");
            log_error();
            continue;
        } else if (seq_mode != NULL || par_mode != NULL) {
            // exec multiple cmds
            char split_buf[MAX_LEN] = {0}; // temp split cmd buffer
            char* delim = seq_mode != NULL ? ";" : "&";
            size_t len = split(input_buf, delim, split_buf);

            // command (+ args) buffers
            char   _f_bufs[len][MAX_LEN];   // formatted char buffers
            char*  _f_strs[len][MAX_LEN/2]; // formatted string buffers
            char** _f_cmds[len];            // formatted multi-cmd buffer
            memset(_f_bufs, 0, sizeof _f_bufs);
            memset(_f_strs, 0, sizeof _f_strs);
            memset(_f_cmds, 0, sizeof _f_cmds);

            // redirection (+ args) buffers
            char   _r_bufs[len][MAX_LEN];   // redirection char buffers
            char*  _r_strs[len][MAX_LEN/2]; // redirection string buffers
            char** _r_cmds[len];            // redirection multi-cmd buffer
            memset(_r_bufs, 0, sizeof _r_bufs);
            memset(_r_strs, 0, sizeof _r_strs);
            memset(_r_cmds, 0, sizeof _r_cmds);

            // redirection array
            char redir_types[len];
            memset(redir_types, 0, sizeof redir_types);

            char* _f_ptr = split_buf;
            int i = len;
            while (i>0) {
                char redir_buf[MAX_LEN] = {0}; // temp redirection buffer
                size_t before_format_len = strlen(_f_ptr) + 1;

                char redir_type = split_redir(_f_ptr, redir_buf);
                if (redir_type == -1)
                    log_error();
                redir_types[len-i] = redir_type;

                // if redirection invalid, set cmd to empty string (won't exec)
                char* _cmd = redir_type == -1 ? "" : _f_ptr;
                    
                format_cmd(_cmd, _f_bufs[len-i]);
                format_cmd(redir_buf, _r_bufs[len-i]);
                buf_to_strs(_f_bufs[len-i], _f_strs[len-i]);
                buf_to_strs(_r_bufs[len-i], _r_strs[len-i]);
                _f_cmds[len-i] = _f_strs[len-i];
                _r_cmds[len-i] = _r_strs[len-i];
                _f_ptr += before_format_len;
                --i;
            }

            int res = 0;
            if (seq_mode != NULL)
                res = exec_cmds_seq(_f_cmds, len, redir_types, _r_cmds);
            else
                res = exec_cmds_par(_f_cmds, len, redir_types, _r_cmds);
            if (res < 0)
                log_error();
        } else {
            // exec single cmd
            char  redir_buf[MAX_LEN] = {0}; // temp redirection buffer
            char  _f_buf[MAX_LEN] = {0};    // formatted char buffer
            char* _f_str[MAX_LEN/2] = {0};  // formatted string buffer
            char  _r_buf[MAX_LEN] = {0};    // redirection char buffer
            char* _r_str[MAX_LEN/2] = {0};  // redirection string buffer

            char redir_type = split_redir(input_buf, redir_buf);
            if (redir_type == -1) {
                log_error();
                continue;
            }
            format_cmd(input_buf, _f_buf); // format cmd (+ args)
            format_cmd(redir_buf, _r_buf); // format redirection info (+ args)
            buf_to_strs(_f_buf, _f_str);
            buf_to_strs(_r_buf, _r_str);
            if (redir_type == '>' && _r_str[1] != NULL) {
                printf_debug("DEBUG: >1 file redirection arg specified\n");
                log_error();
                continue;
            }
            int res = exec_cmd(_f_str, redir_type, _r_str);
            if (res < 0)
                log_error();
        }
    }
    return exit_code;
}
