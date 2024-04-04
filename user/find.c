#include "kernel/types.h"

#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "user/user.h"

char *dst_file;

// get file name from absolute path
const char *get_file_name(const char *abs_path) {
    int i = strlen(abs_path) - 1;
    while (i >= 0 && abs_path[i] != '/')
        i--;
    return abs_path + i + 1;
}

// judge if the file is the target file and print path
void judge_file(const char *file_path) {
    const char *file_name = get_file_name(file_path);
    if (strcmp(file_name, dst_file) == 0)
        printf("%s\n", file_path);
}

void find_files(const char *path) {
    int fd;
    if ((fd = open(path, O_RDONLY)) < 0) {
        fprintf(2, "find: cannot open %s, fd: %d\n", path, fd);
        return;
    }

    struct stat status;
    if (fstat(fd, &status) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    switch (status.type) {
    case T_DEVICE:
    case T_FILE:
        judge_file(path);
        break;
    // read all files and recursively find
    case T_DIR:
        struct dirent entry;
        char path_buf[512] = {0};
        while (read(fd, &entry, sizeof(entry)) == sizeof(entry)) {
            if (entry.inum == 0)
                continue;
            if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
                continue;
            if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(path_buf)) {
                fprintf(2, "find: path too long\n");
                break;
            }
            // append file name to the path
            strcpy(path_buf, path);
            char *p = path_buf + strlen(path);
            *p++ = '/';
            strcpy(p, entry.name);
            find_files(path_buf);
        }
    }
    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(2, "Usage: find <path> <file>\n");
        exit(1);
    }
    dst_file = argv[2];
    find_files(argv[1]);
    exit(0);
}
