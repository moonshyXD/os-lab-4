#include "mapping.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int block_ipc_signals(sigset_t *signals) {
    if (sigemptyset(signals) == -1 ||
        sigaddset(signals, SIGUSR1) == -1 ||
        sigaddset(signals, SIGUSR2) == -1 ||
        sigaddset(signals, SIGCHLD) == -1 ||
        sigprocmask(SIG_BLOCK, signals, NULL) == -1) {
        return -1;
    }
    struct sigaction action = {0};
    action.sa_handler = SIG_DFL;
    if (sigemptyset(&action.sa_mask) == -1 ||
        sigaction(SIGCHLD, &action, NULL) == -1) {
        return -1;
    }
    return 0;
}

int wait_for_ack(const sigset_t *signals, pid_t child) {
    for (;;) {
        siginfo_t info;
        int signal = sigwaitinfo(signals, &info);
        if (signal == -1) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (info.si_pid != child) {
            continue;
        }
        if (signal == SIGUSR2) {
            return 0;
        }
        if (signal == SIGCHLD) {
            errno = ECHILD;
            return -1;
        }
    }
}

int wait_for_command(pid_t parent) {
    sigset_t signals;
    if (sigemptyset(&signals) == -1 || sigaddset(&signals, SIGUSR1) == -1) {
        return -1;
    }
    for (;;) {
        if (getppid() != parent) {
            errno = ECHILD;
            return -1;
        }
        const struct timespec interval = {.tv_sec = 1, .tv_nsec = 0};
        siginfo_t info;
        int signal = sigtimedwait(&signals, &info, &interval);
        if (signal == SIGUSR1 && info.si_pid == parent) {
            return 0;
        }
        if (signal == -1 && errno != EINTR && errno != EAGAIN) {
            return -1;
        }
    }
}

int publish_line(int fd, const char *line, size_t size) {
    off_t length = (off_t)size;
    if (length < 0 || (uintmax_t)length != (uintmax_t)size) {
        errno = EOVERFLOW;
        return -1;
    }
    if (ftruncate(fd, length) == -1) {
        return -1;
    }
    if (size == 0) {
        return 0;
    }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        return -1;
    }
    memcpy(data, line, size);
    return munmap(data, size);
}

char *map_line(int fd, size_t *size) {
    struct stat info;
    *size = 0;
    if (fstat(fd, &info) == -1) {
        return MAP_FAILED;
    }
    if (info.st_size < 0 || (uintmax_t)info.st_size > SIZE_MAX) {
        errno = EOVERFLOW;
        return MAP_FAILED;
    }
    *size = (size_t)info.st_size;
    if (*size == 0) {
        return NULL;
    }
    char *data = mmap(NULL, *size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        return MAP_FAILED;
    }
    if (data[*size - 1] != '\0' || memchr(data, '\0', *size - 1) != NULL) {
        int result = munmap(data, *size);
        if (result == -1) {
            return MAP_FAILED;
        }
        errno = EINVAL;
        return MAP_FAILED;
    }
    return data;
}
