#include "dummy_main.h"
#include <stdio.h>
#include <unistd.h>

int dummy_main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("Dummy B started (PID: %d)\n", getpid());
    for (int i = 1; i <= 8; i++) {
        printf("Dummy B is running. Iteration %d\n", i);
        for (int j = 0; j < 400000000; j++);
    }
    printf("Dummy B finished\n");
    return 0;
}
