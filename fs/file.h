#ifndef FILE_H
#define FILE_H
typedef struct file {
    fs_node_t *node;
    u32int pos;
    u32int flags;
} file_t;

file_t *files[16384];
int current_fd = 2;
#endif