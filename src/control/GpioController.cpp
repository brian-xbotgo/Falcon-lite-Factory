#include "control/GpioController.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace ft {

bool GpioController::exportGpio(int gpio) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "/sys/class/gpio/gpio%d", gpio);
    struct stat st;
    if (stat(buffer, &st) == 0) return true;

    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) {
        std::fprintf(stderr, "[GpioController] open export failed gpio=%d errno=%d %s\n",
                     gpio, errno, std::strerror(errno));
        return false;
    }

    int len = snprintf(buffer, sizeof(buffer), "%d", gpio);
    bool ok = (::write(fd, buffer, len) == len);
    const int savedErrno = errno;
    close(fd);

    if (!ok) {
        snprintf(buffer, sizeof(buffer), "/sys/class/gpio/gpio%d", gpio);
        if (stat(buffer, &st) == 0) return true;
        std::fprintf(stderr, "[GpioController] export failed gpio=%d errno=%d %s\n",
                     gpio, savedErrno, std::strerror(savedErrno));
        return false;
    }

    snprintf(buffer, sizeof(buffer), "/sys/class/gpio/gpio%d", gpio);
    for (int i = 0; i < 50; i++) {
        struct stat st;
        if (stat(buffer, &st) == 0) return true;
        usleep(10 * 1000);
    }
    return false;
}

bool GpioController::setDirection(int gpio, const std::string& dir) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        std::fprintf(stderr, "[GpioController] open direction failed gpio=%d dir=%s errno=%d %s\n",
                     gpio, dir.c_str(), errno, std::strerror(errno));
        return false;
    }
    bool ok = (::write(fd, dir.c_str(), dir.size()) == static_cast<ssize_t>(dir.size()));
    const int savedErrno = errno;
    close(fd);
    if (!ok) {
        std::fprintf(stderr, "[GpioController] write direction failed gpio=%d dir=%s errno=%d %s\n",
                     gpio, dir.c_str(), savedErrno, std::strerror(savedErrno));
    }
    return ok;
}

bool GpioController::setEdge(int gpio, const std::string& edge) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/edge", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        std::fprintf(stderr, "[GpioController] open edge failed gpio=%d edge=%s errno=%d %s\n",
                     gpio, edge.c_str(), errno, std::strerror(errno));
        return false;
    }
    bool ok = (::write(fd, edge.c_str(), edge.size()) == static_cast<ssize_t>(edge.size()));
    const int savedErrno = errno;
    close(fd);
    if (!ok) {
        std::fprintf(stderr, "[GpioController] write edge failed gpio=%d edge=%s errno=%d %s\n",
                     gpio, edge.c_str(), savedErrno, std::strerror(savedErrno));
    }
    return ok;
}

bool GpioController::write(int gpio, int value) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        std::fprintf(stderr, "[GpioController] open value failed gpio=%d value=%d errno=%d %s\n",
                     gpio, value, errno, std::strerror(errno));
        return false;
    }
    char c = value ? '1' : '0';
    bool ok = (::write(fd, &c, 1) == 1);
    const int savedErrno = errno;
    close(fd);
    if (!ok) {
        std::fprintf(stderr, "[GpioController] write value failed gpio=%d value=%d errno=%d %s\n",
                     gpio, value, savedErrno, std::strerror(savedErrno));
    }
    return ok;
}

int GpioController::read(int gpio) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        std::fprintf(stderr, "[GpioController] open read failed gpio=%d errno=%d %s\n",
                     gpio, errno, std::strerror(errno));
        return -1;
    }
    char c = 0;
    ::read(fd, &c, 1);
    close(fd);
    return (c == '1') ? 1 : 0;
}

bool GpioController::unexport(int gpio) {
    char buffer[64];
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd < 0) {
        std::fprintf(stderr, "[GpioController] open unexport failed gpio=%d errno=%d %s\n",
                     gpio, errno, std::strerror(errno));
        return false;
    }
    int len = snprintf(buffer, sizeof(buffer), "%d", gpio);
    bool ok = (::write(fd, buffer, len) == len);
    const int savedErrno = errno;
    close(fd);
    if (!ok) {
        std::fprintf(stderr, "[GpioController] unexport failed gpio=%d errno=%d %s\n",
                     gpio, savedErrno, std::strerror(savedErrno));
    }
    return ok;
}

} // namespace ft
