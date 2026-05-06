#include "hal/GpioController.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace ft {

bool GpioController::exportGpio(int gpio) {
    char buffer[64];
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) return false;

    int len = snprintf(buffer, sizeof(buffer), "%d", gpio);
    bool ok = (::write(fd, buffer, len) == len);
    close(fd);

    if (!ok) {
        snprintf(buffer, sizeof(buffer), "/sys/class/gpio/gpio%d", gpio);
        struct stat st;
        if (stat(buffer, &st) == 0) return true;
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
    if (fd < 0) return false;
    bool ok = (::write(fd, dir.c_str(), dir.size()) == static_cast<ssize_t>(dir.size()));
    close(fd);
    return ok;
}

bool GpioController::setEdge(int gpio, const std::string& edge) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/edge", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return false;
    bool ok = (::write(fd, edge.c_str(), edge.size()) == static_cast<ssize_t>(edge.size()));
    close(fd);
    return ok;
}

bool GpioController::write(int gpio, int value) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return false;
    char c = value ? '1' : '0';
    bool ok = (::write(fd, &c, 1) == 1);
    close(fd);
    return ok;
}

int GpioController::read(int gpio) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char c = 0;
    ::read(fd, &c, 1);
    close(fd);
    return (c == '1') ? 1 : 0;
}

bool GpioController::unexport(int gpio) {
    char buffer[64];
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd < 0) return false;
    int len = snprintf(buffer, sizeof(buffer), "%d", gpio);
    bool ok = (::write(fd, buffer, len) == len);
    close(fd);
    return ok;
}

} // namespace ft
