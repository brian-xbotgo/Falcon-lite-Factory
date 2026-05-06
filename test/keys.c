#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <linux/input.h>
#include <sys/select.h>

#define NUM_DEVS 3

int main(void)
{
    const char *devs[NUM_DEVS] = {
        "/dev/input/event0",   /* adc-m-keys  */
        "/dev/input/event1",   /* adc-ab-keys */
        "/dev/input/event2",   /* gpio-keys   */
    };
    int fd[NUM_DEVS];
    int i, maxfd = 0;

    for (i = 0; i < NUM_DEVS; i++) {
        fd[i] = open(devs[i], O_RDONLY);
        if (fd[i] < 0) {
            perror(devs[i]);
            return 1;
        }
        if (fd[i] > maxfd)
            maxfd = fd[i];
        printf("Opened %s (fd=%d)\n", devs[i], fd[i]);
    }

    printf("\nPress any button to test, Ctrl+C to quit.\n\n");

    while (1) {
        fd_set rfds;
        struct input_event ev;

        FD_ZERO(&rfds);
        for (i = 0; i < NUM_DEVS; i++)
            FD_SET(fd[i], &rfds);

        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }

        for (i = 0; i < NUM_DEVS; i++) {
            if (!FD_ISSET(fd[i], &rfds))
                continue;
            if (read(fd[i], &ev, sizeof(ev)) != sizeof(ev))
                continue;
            if (ev.type == EV_KEY) {
                printf("[%s]  key 0x%04x (%3d)  %s\n",
                       devs[i], ev.code, ev.code,
                       ev.value == 1 ? "PRESSED" :
                       ev.value == 0 ? "RELEASED" : "REPEAT");
            }
        }
    }

    for (i = 0; i < NUM_DEVS; i++)
        close(fd[i]);

    return 0;
}