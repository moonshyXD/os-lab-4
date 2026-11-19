#include "mapping.h"
#include "sum.h"
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static long parse_argument(const char *text, long minimum) {
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < minimum || value > INT_MAX) {
        return -1;
    }
    return value;
}

static int process_line(char *line, size_t size, FILE *output, int *result) {
    Status status;
    long sum = sum_line(line, &status);
    int io_error = 0;
    if (status == STATUS_OK) {
        if (fprintf(output, "%ld\n", sum) < 0 || fflush(output) == EOF) {
            perror("write output");
            io_error = 1;
        } else if (isatty(STDIN_FILENO) && isatty(STDERR_FILENO)) {
            fprintf(stderr, "Сумма: %ld\n", sum);
        }
    } else if (status != STATUS_NO_NUMBERS) {
        *result = EXIT_FAILURE;
    }
    if (munmap(line, size) == -1) {
        perror("munmap command");
        return EXIT_FAILURE;
    }
    if (status == STATUS_IO_ERROR) {
        perror("sum_line");
    }
    if (io_error || status == STATUS_IO_ERROR) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int process_commands(int fd, FILE *output, pid_t parent) {
    if (getppid() != parent || kill(parent, SIGUSR2) == -1) {
        fprintf(stderr, "child: родительский процесс недоступен\n");
        return EXIT_FAILURE;
    }
    int result = EXIT_SUCCESS;
    for (;;) {
        if (wait_for_command(parent) == -1) {
            perror("wait for command");
            return EXIT_FAILURE;
        }
        size_t size;
        char *line = map_line(fd, &size);
        if (line == MAP_FAILED) {
            perror("map command");
            return EXIT_FAILURE;
        }
        if (line == NULL) {
            return result;
        }
        if (process_line(line, size, output, &result) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
        if (getppid() != parent || kill(parent, SIGUSR2) == -1) {
            fprintf(stderr, "child: родительский процесс недоступен\n");
            return EXIT_FAILURE;
        }
    }
}

static int run_child(const char *filename, int descriptor, pid_t parent) {
    sigset_t signals;
    int result = EXIT_FAILURE;
    if (block_ipc_signals(&signals) == -1) {
        perror("block signals");
    } else {
        FILE *output = fopen(filename, "w");
        if (output == NULL) {
            perror("open output");
        } else {
            result = process_commands(descriptor, output, parent);
            if (fclose(output) == EOF) {
                perror("close output");
                result = EXIT_FAILURE;
            }
        }
    }
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 4 || argv[1][0] == '\0') {
        fprintf(stderr, "usage: %s <output> <mapping-fd> <parent-pid>\n", argv[0]);
        return EXIT_FAILURE;
    }
    long descriptor = parse_argument(argv[2], 0);
    long parent = parse_argument(argv[3], 1);
    if (descriptor == -1 || parent == -1) {
        fprintf(stderr, "child: неверный дескриптор или PID\n");
        return EXIT_FAILURE;
    }
    int result = run_child(argv[1], (int)descriptor, (pid_t)parent);
    if (close((int)descriptor) == -1) {
        perror("close mapping file");
        result = EXIT_FAILURE;
    }
    return result;
}
