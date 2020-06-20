#pragma once

bool is_builtin(char const* cmd);

int exec_builtin(char const* cmd, char *const argv[], char const redir_type, char *const redir_argv[]);
int exec_extern(char const* cmd, char *const argv[], char const redir_type, char *const redir_argv[]);

int exec_cmd(char *const argv[], char const redir_type, char *const redir_argv[]);
int exec_cmds_seq(char ***const cmds, size_t len, char const* redir_types, char ***const redir_cmds);
int exec_cmds_par(char ***const cmds, size_t len, char const* redir_types, char ***const redir_cmds);
