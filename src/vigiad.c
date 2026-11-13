/* vigiad - Vigia daemon (phase 0).
 *
 * Samples system metrics at a fixed interval, learns a baseline during a
 * calibration period and then reports how far the system drifts from it.
 * Output is one CSV row per sample. No network access of any kind.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "baseline.h"
#include "collect.h"

static volatile sig_atomic_t running = 1;

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [-i interval_ms] [-c calibration_samples] [-n samples] "
        "[-o file.csv]\n"
        "  -i  sampling interval in milliseconds (default 5000)\n"
        "  -c  samples used to build the baseline (default 720)\n"
        "  -n  stop after this many samples (default 0 = run forever)\n"
        "  -o  CSV output file (default: standard output)\n",
        prog);
}

/* Parse a positive integer option; exits on invalid input. */
static long parse_long(const char *text, long min, long max, const char *what)
{
    char *end;
    errno = 0;
    long v = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || v < min || v > max) {
        fprintf(stderr, "vigiad: invalid %s: %s\n", what, text);
        exit(EXIT_FAILURE);
    }
    return v;
}

static void write_header(FILE *out)
{
    fprintf(out, "time,phase");
    for (int m = 0; m < M_COUNT; m++)
        fprintf(out, ",%s", metric_name(m));
    for (int m = 0; m < M_COUNT; m++)
        fprintf(out, ",mu_%s,sigma_%s", metric_name(m), metric_name(m));
    fprintf(out, ",index,cause,alert\n");
}

static void write_row(FILE *out, const struct sample *s,
                      const struct baseline *b, const struct verdict *v)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    fprintf(out, "%lld.%03ld,%s", (long long)now.tv_sec,
            now.tv_nsec / 1000000L, b->ready ? "monitor" : "calibrate");

    for (int m = 0; m < M_COUNT; m++) {
        if (s->valid[m])
            fprintf(out, ",%.3f", s->value[m]);
        else
            fprintf(out, ",");
    }
    for (int m = 0; m < M_COUNT; m++) {
        if (b->ready && b->count[m] > 0)
            fprintf(out, ",%.3f,%.3f", b->mean[m], b->sigma[m]);
        else
            fprintf(out, ",,");
    }
    if (b->ready)
        fprintf(out, ",%.3f,%s,%d\n", v->index,
                v->cause >= 0 ? metric_name(v->cause) : "", v->alert);
    else
        fprintf(out, ",,,\n");
    fflush(out);
}

int main(int argc, char **argv)
{
    long interval_ms = 5000, calibration = 720, limit = 0;
    const char *out_path = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "i:c:n:o:h")) != -1) {
        switch (opt) {
        case 'i': interval_ms = parse_long(optarg, 100, 3600000, "interval"); break;
        case 'c': calibration = parse_long(optarg, 2, 10000000, "calibration"); break;
        case 'n': limit = parse_long(optarg, 0, 100000000, "sample count"); break;
        case 'o': out_path = optarg; break;
        default:  usage(argv[0]); return opt == 'h' ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    FILE *out = stdout;
    if (out_path) {
        out = fopen(out_path, "w");
        if (!out) {
            fprintf(stderr, "vigiad: cannot open %s: %s\n", out_path,
                    strerror(errno));
            return EXIT_FAILURE;
        }
    }

    struct sigaction sa = { .sa_handler = on_signal };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct baseline base;
    baseline_init(&base);
    collect_init();
    write_header(out);

    fprintf(stderr, "vigiad: sampling every %ld ms, calibrating for %ld samples\n",
            interval_ms, calibration);

    /* Absolute deadlines keep the sampling period free of drift. */
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    bool was_alert = false;
    for (long n = 0; running && (limit == 0 || n < limit); n++) {
        next.tv_sec  += interval_ms / 1000;
        next.tv_nsec += (interval_ms % 1000) * 1000000L;
        if (next.tv_nsec >= 1000000000L) {
            next.tv_sec++;
            next.tv_nsec -= 1000000000L;
        }
        while (running &&
               clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) == EINTR)
            ;
        if (!running)
            break;

        struct sample s;
        struct verdict v = { .cause = -1 };
        collect_sample(&s);

        if (!base.ready) {
            baseline_learn(&base, &s);
            write_row(out, &s, &base, &v);
            if (n + 1 >= calibration) {
                baseline_freeze(&base);
                fprintf(stderr, "vigiad: baseline ready\n");
                for (int m = 0; m < M_COUNT; m++) {
                    if (base.count[m] > 0)
                        fprintf(stderr, "vigiad:   %-16s mu=%.2f sigma=%.2f\n",
                                metric_name(m), base.mean[m], base.sigma[m]);
                    else
                        fprintf(stderr, "vigiad:   %-16s unavailable\n",
                                metric_name(m));
                }
            }
            continue;
        }

        baseline_judge(&base, &s, &v);
        write_row(out, &s, &base, &v);

        if (v.alert && !was_alert)
            fprintf(stderr, "vigiad: ALERT system slower than baseline "
                    "(index %.1f, probable cause: %s)\n", v.index,
                    v.cause >= 0 ? metric_label(v.cause) : "unknown");
        else if (!v.alert && was_alert)
            fprintf(stderr, "vigiad: system back to normal\n");
        was_alert = v.alert;
    }

    if (out != stdout)
        fclose(out);
    fprintf(stderr, "vigiad: stopped\n");
    return EXIT_SUCCESS;
}
