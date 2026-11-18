#ifndef MAPPING_H
#define MAPPING_H

#include <signal.h>
#include <stddef.h>
#include <sys/types.h>

int block_ipc_signals(sigset_t *signals);
int wait_for_ack(const sigset_t *signals, pid_t child);
int wait_for_command(pid_t parent);
int publish_line(int fd, const char *line, size_t size);
char *map_line(int fd, size_t *size);

#endif
