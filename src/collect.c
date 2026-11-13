/* collect.c - system metric collectors.
 *
 * Every collector reads a small text file exposed by the kernel. A source
 * that is missing or unreadable only marks its metric as invalid: the
 * daemon keeps running with the remaining ones.
 */
#define _POSIX_C_SOURCE 200809L
#include "collect.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define LINE_MAX_LEN 256
#define PATH_MAX_LEN 128

static const char *const names[M_COUNT] = {
    "cpu_pct", "mem_pct", "swap_pct", "temp_c",
    "psi_cpu_pct", "psi_mem_pct", "psi_io_pct", "latency_us",
};

static const char *const labels[M_COUNT] = {
    "CPU usage", "RAM usage", "swap usage", "CPU temperature",
    "CPU pressure", "memory pressure", "I/O pressure", "wake-up latency",
};

const char *metric_name(enum metric m)  { return names[m]; }
const char *metric_label(enum metric m) { return labels[m]; }

/* Previous readings of the cumulative counters. */
static unsigned long long prev_cpu_total, prev_cpu_idle;
static unsigned long long prev_psi[3];
static bool               prev_psi_ok[3];
static struct timespec    prev_time;
static char               temp_path[PATH_MAX_LEN];

static double elapsed_us(const struct timespec *a, const struct timespec *b)
{
    return (double)(b->tv_sec - a->tv_sec) * 1e6 +
           (double)(b->tv_nsec - a->tv_nsec) / 1e3;
}

/* /proc/stat, first line: cumulative jiffies per CPU state. */
static bool read_cpu(unsigned long long *total, unsigned long long *idle)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f)
        return false;

    unsigned long long v[8] = {0};
    int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
    fclose(f);
    if (n < 4)
        return false;

    *total = 0;
    for (int i = 0; i < 8; i++)
        *total += v[i];
    *idle = v[3] + v[4];            /* idle + iowait */
    return true;
}

/* /proc/meminfo: RAM and swap usage as percentages. */
static void read_mem(struct sample *s)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return;

    unsigned long long total = 0, avail = 0, swap_total = 0, swap_free = 0;
    bool have_total = false, have_avail = false;
    char line[LINE_MAX_LEN];

    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "MemTotal: %llu", &total) == 1)
            have_total = true;
        else if (sscanf(line, "MemAvailable: %llu", &avail) == 1)
            have_avail = true;
        else if (sscanf(line, "SwapTotal: %llu", &swap_total) == 1)
            continue;
        else if (sscanf(line, "SwapFree: %llu", &swap_free) == 1)
            continue;
    }
    fclose(f);

    if (have_total && have_avail && total > 0 && avail <= total) {
        s->value[M_MEM] = 100.0 * (double)(total - avail) / (double)total;
        s->valid[M_MEM] = true;
    }
    if (swap_total > 0 && swap_free <= swap_total) {
        s->value[M_SWAP] =
            100.0 * (double)(swap_total - swap_free) / (double)swap_total;
        s->valid[M_SWAP] = true;
    }
}

/* /proc/pressure/<res>: "some ... total=<microseconds stalled>". */
static bool read_psi(const char *resource, unsigned long long *total)
{
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof path, "/proc/pressure/%s", resource);

    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    char line[LINE_MAX_LEN];
    bool ok = false;
    if (fgets(line, sizeof line, f)) {
        const char *p = strstr(line, "total=");
        if (p && sscanf(p, "total=%llu", total) == 1)
            ok = true;
    }
    fclose(f);
    return ok;
}

/* Find the CPU temperature sensor among the hwmon devices. */
static void find_temp_sensor(void)
{
    static const char *const known[] = {
        "k10temp", "coretemp", "zenpower", "cpu_thermal",
    };

    for (int i = 0; i < 16; i++) {
        char path[PATH_MAX_LEN], name[32] = "";
        snprintf(path, sizeof path, "/sys/class/hwmon/hwmon%d/name", i);

        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        if (fgets(name, sizeof name, f))
            name[strcspn(name, "\n")] = '\0';
        fclose(f);

        for (size_t k = 0; k < sizeof known / sizeof known[0]; k++) {
            if (strcmp(name, known[k]) == 0) {
                snprintf(temp_path, sizeof temp_path,
                         "/sys/class/hwmon/hwmon%d/temp1_input", i);
                return;
            }
        }
    }
}

static void read_temp(struct sample *s)
{
    if (temp_path[0] == '\0')
        return;

    FILE *f = fopen(temp_path, "r");
    if (!f)
        return;

    long millideg;
    if (fscanf(f, "%ld", &millideg) == 1) {
        s->value[M_TEMP] = (double)millideg / 1000.0;
        s->valid[M_TEMP] = true;
    }
    fclose(f);
}

/* Responsiveness probe: ask for a 10 ms sleep and measure how late the
 * scheduler wakes us up. A busy system wakes us later. */
static void read_latency(struct sample *s)
{
    const struct timespec request = { .tv_sec = 0, .tv_nsec = 10000000L };
    struct timespec before, after;

    if (clock_gettime(CLOCK_MONOTONIC, &before) != 0)
        return;
    nanosleep(&request, NULL);
    if (clock_gettime(CLOCK_MONOTONIC, &after) != 0)
        return;

    double late = elapsed_us(&before, &after) - 10000.0;
    s->value[M_LATENCY] = late > 0.0 ? late : 0.0;
    s->valid[M_LATENCY] = true;
}

static const char *const psi_resource[3] = { "cpu", "memory", "io" };
static const enum metric psi_metric[3] = { M_PSI_CPU, M_PSI_MEM, M_PSI_IO };

void collect_init(void)
{
    find_temp_sensor();
    read_cpu(&prev_cpu_total, &prev_cpu_idle);
    for (int i = 0; i < 3; i++)
        prev_psi_ok[i] = read_psi(psi_resource[i], &prev_psi[i]);
    clock_gettime(CLOCK_MONOTONIC, &prev_time);
}

void collect_sample(struct sample *s)
{
    memset(s, 0, sizeof *s);

    read_latency(s);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double dt_us = elapsed_us(&prev_time, &now);

    unsigned long long total, idle;
    if (read_cpu(&total, &idle) && total > prev_cpu_total) {
        double d_total = (double)(total - prev_cpu_total);
        double d_idle  = (double)(idle - prev_cpu_idle);
        s->value[M_CPU] = 100.0 * (d_total - d_idle) / d_total;
        s->valid[M_CPU] = true;
        prev_cpu_total = total;
        prev_cpu_idle  = idle;
    }

    for (int i = 0; i < 3; i++) {
        unsigned long long stalled;
        bool ok = read_psi(psi_resource[i], &stalled);
        if (ok && prev_psi_ok[i] && dt_us > 0.0 && stalled >= prev_psi[i]) {
            double pct = 100.0 * (double)(stalled - prev_psi[i]) / dt_us;
            s->value[psi_metric[i]] = pct > 100.0 ? 100.0 : pct;
            s->valid[psi_metric[i]] = true;
        }
        prev_psi[i]    = stalled;
        prev_psi_ok[i] = ok;
    }

    read_mem(s);
    read_temp(s);

    prev_time = now;
}
