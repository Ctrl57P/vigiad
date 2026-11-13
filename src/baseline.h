/* baseline.h - statistical model of the "normal" system and its deviation. */
#ifndef VIGIA_BASELINE_H
#define VIGIA_BASELINE_H

#include <stdbool.h>
#include "collect.h"

#define EWMA_ALPHA       0.3   /* weight of the newest sample              */
#define ALERT_THRESHOLD  2.0   /* index above this = outside normal (2 sd) */
#define ALERT_PERSIST    5     /* consecutive samples to switch the alert  */

struct baseline {
    /* Calibration: running mean and variance (Welford's algorithm). */
    long   count[M_COUNT];
    double mean[M_COUNT];
    double m2[M_COUNT];        /* sum of squared distances to the mean */
    double sigma[M_COUNT];     /* fixed when calibration ends           */
    bool   ready;

    /* Monitoring. */
    double ewma[M_COUNT];      /* smoothed z-score per metric */
    bool   ewma_started[M_COUNT];
    int    streak;             /* samples in a row contradicting `alert` */
    bool   alert;
};

struct verdict {
    double z[M_COUNT];         /* smoothed z-score, 0 if metric invalid   */
    double index;              /* degradation index: worst smoothed z     */
    int    cause;              /* metric behind the index, -1 if none     */
    bool   alert;
};

void baseline_init(struct baseline *b);

/* Calibration phase: learn what "normal" looks like. */
void baseline_learn(struct baseline *b, const struct sample *s);

/* End calibration: freeze mean and standard deviation. */
void baseline_freeze(struct baseline *b);

/* Monitoring phase: compare one sample against the baseline. */
void baseline_judge(struct baseline *b, const struct sample *s,
                    struct verdict *v);

#endif
