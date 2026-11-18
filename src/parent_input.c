#include "parent_input.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t child_changed;

static void wake_on_child_change(int signal_number) {
    (void)signal_number;
    child_changed = 1;
}

int prepare_input_wait(sigset_t *input_mask) {
    struct sigaction action = {0};
    action.sa_handler = wake_on_child_change;
    if (sigemptyset(&action.sa_mask) == -1 || sigaction(SIGCHLD, &action, NULL) == -1 ||
        sigprocmask(SIG_BLOCK, NULL, input_mask) == -1 ||
        sigdelset(input_mask, SIGCHLD) == -1) {
        perror("prepare stdin signal mask");
        return -1;
    }
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL) == -1) {
        perror("ignore SIGPIPE");
        return -1;
    }
    return 0;
}

static int check_child(pid_t child) {
    siginfo_t state = {0};
    int result;
    do {
        result = waitid(P_PID, child, &state, WEXITED | WSTOPPED | WNOHANG | WNOWAIT);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
        perror("waitid child");
        return -1;
    }
    if (state.si_pid != child) {
        return 0;
    }
    if (state.si_code == CLD_STOPPED) {
        fprintf(stderr, "parent: дочерний процесс остановлен\n");
    } else {
        fprintf(stderr, "parent: дочерний процесс завершился во время ввода\n");
    }
    return -1;
}

static int wait_for_input(pid_t child, const sigset_t *input_mask) {
    for (;;) {
        if (child_changed) {
            child_changed = 0;
            if (check_child(child) == -1) {
                return -1;
            }
        }
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(STDIN_FILENO, &readable);
        if (pselect(STDIN_FILENO + 1, &readable, NULL, NULL, NULL, input_mask) >= 0) {
            return 0;
        }
        if (errno != EINTR) {
            perror("pselect stdin");
            return -1;
        }
    }
}

static ssize_t fill_buffer(Input *input, pid_t child, const sigset_t *input_mask) {
    for (;;) {
        if (child > 0 && wait_for_input(child, input_mask) == -1) {
            return -1;
        }
        ssize_t count = read(STDIN_FILENO, input->buffer, sizeof(input->buffer));
        if (count >= 0) {
            input->next = 0;
            input->end = (size_t)count;
            return count;
        }
        if (errno != EINTR) {
            perror("read stdin");
            return -1;
        }
    }
}

static int append(char **line, size_t *length, size_t *capacity,
                  const char *data, size_t count) {
    if (count >= SIZE_MAX - *length) {
        errno = EOVERFLOW;
        return -1;
    }
    size_t required = *length + count + 1;
    if (required > *capacity) {
        size_t grown = *capacity <= SIZE_MAX / 2 ? *capacity * 2 : SIZE_MAX;
        size_t new_capacity = grown < required ? required : grown;
        char *buffer = realloc(*line, new_capacity);
        if (buffer == NULL) {
            return -1;
        }
        *line = buffer;
        *capacity = new_capacity;
    }
    memcpy(*line + *length, data, count);
    *length += count;
    (*line)[*length] = '\0';
    return 0;
}

static Status read_line(Input *input, pid_t child, const sigset_t *input_mask,
                        char **out, size_t *out_len) {
    *out = NULL;
    *out_len = 0;
    char *line = NULL;
    size_t length = 0;
    size_t capacity = 0;
    for (;;) {
        if (input->next == input->end) {
            ssize_t count = fill_buffer(input, child, input_mask);
            if (count == -1) {
                free(line);
                return STATUS_IO_ERROR;
            }
            if (count == 0) {
                break;
            }
        }
        const char *start = input->buffer + input->next;
        size_t available = input->end - input->next;
        const char *newline = memchr(start, '\n', available);
        size_t count = newline == NULL ? available : (size_t)(newline - start) + 1;
        if (memchr(start, '\0', count) != NULL) {
            free(line);
            fprintf(stderr, "parent: нулевой байт во входных данных\n");
            return STATUS_IO_ERROR;
        }
        if (append(&line, &length, &capacity, start, count) == -1) {
            perror("allocate input line");
            free(line);
            return STATUS_IO_ERROR;
        }
        input->next += count;
        if (newline != NULL) {
            break;
        }
    }
    if (line == NULL) {
        return STATUS_END_OF_INPUT;
    }
    *out = line;
    *out_len = length;
    return STATUS_OK;
}

Status read_filename(Input *input, char **out) {
    size_t length;
    Status status = read_line(input, 0, NULL, out, &length);
    if (status != STATUS_OK) {
        return status;
    }
    char *line = *out;
    if (line[length - 1] == '\n') {
        line[--length] = '\0';
        if (length > 0 && line[length - 1] == '\r') {
            line[--length] = '\0';
        }
    }
    if (length == 0) {
        fprintf(stderr, "parent: пустое имя выходного файла\n");
        free(line);
        *out = NULL;
        return STATUS_IO_ERROR;
    }
    return STATUS_OK;
}

Status read_command_line(Input *input, pid_t child, const sigset_t *input_mask,
                         char **out, size_t *out_len) {
    Status status = read_line(input, child, input_mask, out, out_len);
    if (status != STATUS_OK) {
        return status;
    }
    if ((*out_len == 1 && (*out)[0] == '\n') ||
        (*out_len == 2 && memcmp(*out, "\r\n", 2) == 0)) {
        free(*out);
        *out = NULL;
        *out_len = 0;
        return STATUS_END_OF_INPUT;
    }
    return STATUS_OK;
}
