#include "mapping.h"
#include "parent_input.h"
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static void start_child(int fd, pid_t parent, char *filename) {
    char descriptor[sizeof(int) * CHAR_BIT + 1];
    char parent_id[sizeof(pid_t) * CHAR_BIT + 1];
    if (snprintf(descriptor, sizeof(descriptor), "%d", fd) < 0 ||
        snprintf(parent_id, sizeof(parent_id), "%ld", (long)parent) < 0) {
        fprintf(stderr, "child: не удалось подготовить аргументы запуска\n");
        _exit(EXIT_FAILURE);
    }
    execv("./child", (char *[]){"./child", filename, descriptor, parent_id, NULL});
    perror("execv child");
    _exit(EXIT_FAILURE);
}

static int send_commands(Input *input, int fd, pid_t parent, pid_t child,
                         const sigset_t *signals) {
    if (wait_for_ack(signals, child) == -1) {
        perror("child readiness");
        return EXIT_FAILURE;
    }
    sigset_t input_mask;
    if (prepare_input_wait(&input_mask) == -1) {
        return EXIT_FAILURE;
    }
    if (printf("parent PID=%ld, child PID=%ld\n", (long)parent, (long)child) < 0 ||
        fflush(stdout) == EOF) {
        perror("write process IDs");
        return EXIT_FAILURE;
    }
    for (;;) {
        char *line = NULL;
        size_t length = 0;
        Status status = read_command_line(input, child, &input_mask, &line, &length);
        if (status != STATUS_OK && status != STATUS_END_OF_INPUT) {
            fprintf(stderr, "parent: ошибка чтения команды\n");
            return EXIT_FAILURE;
        }
        size_t size = status == STATUS_END_OF_INPUT ? 0 : length + 1;
        int result = publish_line(fd, line, size);
        free(line);
        if (result == -1 || kill(child, SIGUSR1) == -1) {
            perror("publish command");
            return EXIT_FAILURE;
        }
        if (status == STATUS_END_OF_INPUT) {
            return EXIT_SUCCESS;
        }
        if (wait_for_ack(signals, child) == -1) {
            perror("child acknowledgement");
            return EXIT_FAILURE;
        }
    }
}

static int finish_child(pid_t child, int result) {
    if (result != EXIT_SUCCESS && kill(child, SIGKILL) == -1 && errno != ESRCH) {
        perror("kill child");
    }
    int status;
    for (;;) {
        if (waitpid(child, &status, WUNTRACED) == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("waitpid");
            return EXIT_FAILURE;
        }
        if (!WIFSTOPPED(status)) {
            break;
        }
        fprintf(stderr, "parent: дочерний процесс остановлен\n");
        result = EXIT_FAILURE;
        if (kill(child, SIGKILL) == -1 && errno != ESRCH) {
            perror("kill stopped child");
            return EXIT_FAILURE;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
        fprintf(stderr, "parent: дочерний процесс завершился с ошибкой\n");
        return EXIT_FAILURE;
    }
    return result;
}

static int create_mapping_file(void) {
    char path[] = "/tmp/lab4-XXXXXX";
    int fd = mkstemp(path);
    if (fd == -1) {
        perror("mkstemp");
        return -1;
    }
    if (unlink(path) == -1) {
        perror("unlink mapping file");
        if (close(fd) == -1) {
            perror("close mapping file");
        }
        return -1;
    }
    return fd;
}

static int run_parent(Input *input, char *filename) {
    sigset_t signals;
    if (block_ipc_signals(&signals) == -1) {
        perror("block signals");
        return EXIT_FAILURE;
    }
    int fd = create_mapping_file();
    if (fd == -1) {
        return EXIT_FAILURE;
    }
    pid_t parent = getpid();
    pid_t child = fork();
    if (child == 0) {
        start_child(fd, parent, filename);
    }
    int result = EXIT_FAILURE;
    if (child == -1) {
        perror("fork");
    } else {
        result = send_commands(input, fd, parent, child, &signals);
        result = finish_child(child, result);
    }
    if (close(fd) == -1) {
        perror("close mapping file");
        result = EXIT_FAILURE;
    }
    return result;
}

int main(void) {
    Input input = {0};
    char *filename = NULL;
    if (read_filename(&input, &filename) != STATUS_OK) {
        fprintf(stderr, "parent: не удалось прочитать имя файла\n");
        return EXIT_FAILURE;
    }
    int result = run_parent(&input, filename);
    free(filename);
    return result;
}
