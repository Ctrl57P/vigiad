/* collect.h - system metric collectors (read-only, /proc and /sys). */
#ifndef VIGIA_COLLECT_H
#define VIGIA_COLLECT_H

#include <stdbool.h>

/* Index of every metric in a sample. "Higher is worse" for all of them. */
enum metric {
    M_CPU = 0,   /* CPU usage, percent of all cores            */
    M_MEM,       /* RAM in use, percent                        */
    M_SWAP,      /* swap in use, percent                       */
    M_TEMP,      /* CPU temperature, degrees Celsius           */
    M_PSI_CPU,   /* time tasks waited for CPU, percent         */
    M_PSI_MEM,   /* time tasks waited for memory, percent      */
    M_PSI_IO,    /* time tasks waited for I/O, percent         */
    M_LATENCY,   /* wake-up delay of a 10 ms sleep, microsecs  */
    M_COUNT
};

struct sample {
    double value[M_COUNT];
    bool   valid[M_COUNT];   /* false when the source is unavailable */
};

/* Short machine name (CSV header) and human description of a metric. */
const char *metric_name(enum metric m);
const char *metric_label(enum metric m);

/* Locate sensors and take the first reading of the cumulative counters. */
void collect_init(void);

/* Fill one sample. Rates are computed against the previous call. */
void collect_sample(struct sample *s);

#endif
