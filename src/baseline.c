/* baseline.c - the model.
 *
 *   calibration:  mean (mu) and standard deviation (sigma) per metric
 *   monitoring:   z = (x - mu) / sigma          how far from normal
 *                 ewma = a*z + (1-a)*ewma       ignore isolated spikes
 *                 index = max(ewma_i, 0)        worst metric right now
 *                 alert when index >= 2 for several samples in a row
 */
#include "baseline.h"

#include <math.h>
#include <string.h>

/* Minimum sigma per metric. An idle system can be so stable that sigma is
 * almost zero, which would turn any tiny change into a huge z-score. The
 * floor is the smallest change we consider meaningful for that metric. */
static const double sigma_floor[M_COUNT] = {
    [M_CPU]     = 2.0,     /* percentage points */
    [M_MEM]     = 1.0,
    [M_SWAP]    = 1.0,
    [M_TEMP]    = 2.0,     /* degrees Celsius   */
    [M_PSI_CPU] = 1.0,
    [M_PSI_MEM] = 1.0,
    [M_PSI_IO]  = 1.0,
    [M_LATENCY] = 100.0,   /* microseconds      */
};

void baseline_init(struct baseline *b)
{
    memset(b, 0, sizeof *b);
}

void baseline_learn(struct baseline *b, const struct sample *s)
{
    for (int m = 0; m < M_COUNT; m++) {
        if (!s->valid[m])
            continue;
        b->count[m]++;
        double delta = s->value[m] - b->mean[m];
        b->mean[m] += delta / (double)b->count[m];
        b->m2[m]   += delta * (s->value[m] - b->mean[m]);
    }
}

void baseline_freeze(struct baseline *b)
{
    for (int m = 0; m < M_COUNT; m++) {
        double sd = 0.0;
        if (b->count[m] > 1)
            sd = sqrt(b->m2[m] / (double)(b->count[m] - 1));
        b->sigma[m] = sd > sigma_floor[m] ? sd : sigma_floor[m];
    }
    b->ready = true;
}

void baseline_judge(struct baseline *b, const struct sample *s,
                    struct verdict *v)
{
    memset(v, 0, sizeof *v);
    v->cause = -1;

    for (int m = 0; m < M_COUNT; m++) {
        if (!s->valid[m] || b->count[m] == 0)
            continue;

        double z = (s->value[m] - b->mean[m]) / b->sigma[m];
        if (!b->ewma_started[m]) {
            b->ewma[m] = z;
            b->ewma_started[m] = true;
        } else {
            b->ewma[m] = EWMA_ALPHA * z + (1.0 - EWMA_ALPHA) * b->ewma[m];
        }
        v->z[m] = b->ewma[m];

        if (b->ewma[m] > v->index) {
            v->index = b->ewma[m];
            v->cause = m;
        }
    }

    /* Hysteresis: the alert only flips after ALERT_PERSIST samples in a
     * row disagree with its current state. */
    bool over = v->index >= ALERT_THRESHOLD;
    if (over != b->alert) {
        if (++b->streak >= ALERT_PERSIST) {
            b->alert = over;
            b->streak = 0;
        }
    } else {
        b->streak = 0;
    }
    v->alert = b->alert;
}
