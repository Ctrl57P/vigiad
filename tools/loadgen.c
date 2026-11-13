/* loadgen - controlled load generator for the demo experiment.
 *
 *   loadgen cpu <threads> <seconds>   busy-loop on <threads> cores
 *   loadgen mem <megabytes> <seconds> allocate and hold that much RAM
 *
 * It exists so the experiment is reproducible without extra packages.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_THREADS 256

static time_t deadline;

static void *burn(void *arg)
{
    (void)arg;
    volatile unsigned long x = 0;
    while (time(NULL) < deadline)
        for (int i = 0; i < 1000000; i++)
            x += (unsigned long)i;
    (void)x;
    return NULL;
}

static long parse_arg(const char *text, long min, long max)
{
    char *end;
    errno = 0;
    long v = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || v < min || v > max) {
        fprintf(stderr, "loadgen: invalid value: %s\n", text);
        exit(EXIT_FAILURE);
    }
    return v;
}

static int run_cpu(long threads, long seconds)
{
    pthread_t tid[MAX_THREADS];
    deadline = time(NULL) + seconds;

    long started = 0;
    for (; started < threads; started++)
        if (pthread_create(&tid[started], NULL, burn, NULL) != 0)
            break;
    for (long i = 0; i < started; i++)
        pthread_join(tid[i], NULL);
    return started == threads ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int run_mem(long megabytes, long seconds)
{
    size_t size = (size_t)megabytes * 1024 * 1024;
    unsigned char *block = malloc(size);
    if (!block) {
        fprintf(stderr, "loadgen: cannot allocate %ld MB\n", megabytes);
        return EXIT_FAILURE;
    }

    /* Touch every page so the kernel really assigns the memory. */
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
        page = 4096;
    for (size_t off = 0; off < size; off += (size_t)page)
        block[off] = 1;

    sleep((unsigned)seconds);
    free(block);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s cpu <threads> <seconds>\n"
                        "       %s mem <megabytes> <seconds>\n",
                argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    long seconds = parse_arg(argv[3], 1, 3600);
    if (strcmp(argv[1], "cpu") == 0)
        return run_cpu(parse_arg(argv[2], 1, MAX_THREADS), seconds);
    if (strcmp(argv[1], "mem") == 0)
        return run_mem(parse_arg(argv[2], 1, 1024 * 1024), seconds);

    fprintf(stderr, "loadgen: unknown mode: %s\n", argv[1]);
    return EXIT_FAILURE;
}
