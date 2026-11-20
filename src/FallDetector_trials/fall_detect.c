/* Impact trigger and random-forest evaluation. See fall_detect.h. */
#include "fall_detect.h"
#include "fall_model.h"
#include "fall_model_fusion.h"
#include <math.h>
#include <string.h>

void fd_init(fd_t *d) { memset(d, 0, sizeof *d); }

static float forest(const float *f, int n_trees, const uint16_t *root, const int8_t *feat, const float *thr,
                    const uint16_t *left, const uint16_t *right, const float *val) {
    float s = 0;
    for (int t = 0; t < n_trees; t++) {
        uint16_t n = root[t];
        while (feat[n] >= 0) n = (f[feat[n]] <= thr[n]) ? left[n] : right[n];
        s += val[n];
    }
    return s / n_trees;
}

float fd_predict(const float *f) {
    return forest(f, FM_N_TREES, FM_ROOT, FM_FEAT, FM_THR, FM_LEFT, FM_RIGHT, FM_VAL);
}

float fd_predict_fusion(const float *wrist14, const float *waist15) {
    float f[FF_N_FEATURES];
    memcpy(f, wrist14, 14 * sizeof(float));
    memcpy(f + 14, waist15, 15 * sizeof(float));
    if (isnan(f[28])) f[28] = FF_DH_FILL;        /* no barometer samples: training median */
    return forest(f, FF_N_TREES, FF_ROOT, FF_FEAT, FF_THR, FF_LEFT, FF_RIGHT, FF_VAL);
}

float fd_threshold_fusion(void) { return FF_THRESHOLD; }

float fd_threshold(void) { return FM_THRESHOLD; }

int fd_push(fd_t *d, float ax, float ay, float az, float gx, float gy, float gz) {
    float *slot = d->buf[d->n % FD_WIN];
    slot[0]=ax; slot[1]=ay; slot[2]=az; slot[3]=gx; slot[4]=gy; slot[5]=gz;
    float g = sqrtf(ax*ax + ay*ay + az*az);
    uint32_t n = d->n++;
    if (d->state == 0) {
        if (g >= FD_TRIG_G && n >= FD_PRE) { d->state = 1; d->cross_n = n; d->peak_n = n; d->peak_g = g; }
    } else if (d->state == 1) {
        if (g > d->peak_g) { d->peak_g = g; d->peak_n = n; }
        if (n - d->cross_n >= (uint32_t)FD_FS) d->state = 2;        /* peak locked after 1 s */
    }
    if (d->state == 2 && n == d->peak_n + FD_POST - 1) {
        static float win[FD_WIN * 6]; static float f[14];
        uint32_t start = d->peak_n - FD_PRE;
        for (int i = 0; i < FD_WIN; i++) memcpy(win + 6*i, d->buf[(start + i) % FD_WIN], 6*sizeof(float));
        fd_features(win, f); d->last_prob = fd_predict(f); d->last_peak_g = d->peak_g; memcpy(d->last_feat, f, sizeof f); d->n_eval++; d->state = 0;
        return d->last_prob >= FM_THRESHOLD;
    }
    return 0;
}
