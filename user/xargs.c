#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"

// #define debug(fmt, ...) printf(fmt, ##__VA_ARGS__)
#define debug(fmt, ...)

const int stdin = 0;
const int BUF_LEN = 512;

int read_line(int fd, char *buf, int buf_len) {
    char *p = buf;
    int cnt = 0;
    while (p - buf < buf_len) {
        if ((cnt += read(fd, p, 1)) <= 0)
            break;
        if (*p == '\n') {
            break;
        }
        p++;
    }
    return cnt;
}

int set_argv(char *buf, char **argv) {
    int argc = 0;
    char *p = buf;
    while (*p) {
        while (*p == ' ')
            p++;
        if (*p == '\0')
            break;
        argv[argc++] = p;
        while (*p != ' ' && *p != '\0')
            p++;
        if (*p == '\0')
            break;
        *p++ = '\0';
    }
    return argc;
}

int main(int argc, char *argv[]) {
    char ori_cmd_buf[BUF_LEN >> 3];
    memset(ori_cmd_buf, 0, sizeof(ori_cmd_buf));
    // construct original argv
    char *cmd = argc > 1 ? argv[1] : "echo";
    strcpy(ori_cmd_buf, cmd);
    char *p = ori_cmd_buf + strlen(cmd);
    // '\0' -> ' '
    *p++ = ' ';
    for (int i = 2; i < argc; i++) {
        strcpy(p, argv[i]);
        p += strlen(argv[i]);
        *p++ = ' ';
    }
    while (1) {
        char buf[BUF_LEN];
        memset(buf, 0, sizeof(buf));
        memmove(buf, ori_cmd_buf, p - ori_cmd_buf);
        // read line from stdin
        char *arg_p = buf + (p - ori_cmd_buf);
        int line_len = read_line(stdin, arg_p, BUF_LEN - (arg_p - buf));
        if (line_len <= 0)
            break;
        // '\n' -> '\0'
        arg_p[line_len - 1] = '\0';
        
        char *subp_argv[32];
        memset(subp_argv, 0, sizeof(subp_argv));
        int subp_argc = set_argv(buf, subp_argv);

        debug("subp_argv: ");
        for (int i = 0; i < subp_argc; i++) {
            debug("%s ", subp_argv[i]);
        }
        debug("\n");

        int pid = fork();
        if (pid == 0) {
            exec(cmd, subp_argv);
        } else {
            int status;
            wait(&status);
            memset(buf, 0, line_len);
        }
    }

    exit(0);
}
