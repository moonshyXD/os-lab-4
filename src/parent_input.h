#ifndef PARENT_INPUT_H
#define PARENT_INPUT_H

#include <signal.h>
#include <stddef.h>
#include <sys/types.h>
#include "status.h"

typedef struct {
    char buffer[4096];
    size_t next;
    size_t end;
} Input;

int prepare_input_wait(sigset_t *input_mask);
Status read_filename(Input *input, char **out);
Status read_command_line(Input *input, pid_t child, const sigset_t *input_mask,
                         char **out, size_t *out_len);

#endif
