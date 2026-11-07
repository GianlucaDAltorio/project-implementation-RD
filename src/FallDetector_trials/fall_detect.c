/* Window features, random-forest evaluation and the streaming trigger state machine.
 * State: 0 idle, 1 locking the peak for 1 s after the first threshold crossing,
 * 2 waiting for the 2 s of post-impact samples, then evaluate. See fall_detect.h. */
#include "fall_detect.h"
#include "fall_model.h"
#include <math.h>
#include <string.h>

void fd_init(fd_t *d) { memset(d, 0, sizeof *d); }

static float mag3(const float *v) { return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); }

void fd_features(const float *w, float *o) {
    static float am[FD_WIN], gm[FD_WIN];
    for (int i = 0; i < FD_WIN; i++) { am[i] = mag3(w + 6*i); gm[i] = mag3(w + 6*i + 3); }
    const int p = FD_PRE, post0 = p + FD_FS/2;
    float pre_min = 1e9f, pre_max = -1e9f, pre_mean = 0, peak = 0;
    for (int i = 0; i < p; i++) { if (am[i] < pre_min) pre_min = am[i]; if (am[i] > pre_max) pre_max = am[i]; pre_mean += am[i]; }
    pre_mean /= p; float pre_var = 0; for (int i = 0; i < p; i++) pre_var += (am[i]-pre_mean)*(am[i]-pre_mean);
    for (int i = p - FD_FS/10; i < p + FD_FS/10; i++) if (am[i] > peak) peak = am[i];
    int np = FD_WIN - post0; float pm = 0, pdev = 0, pmax = -1e9f;
    for (int i = post0; i < FD_WIN; i++) { pm += am[i]; pdev += fabsf(am[i]-1.0f); if (am[i] > pmax) pmax = am[i]; }
    pm /= np; float pvar = 0; for (int i = post0; i < FD_WIN; i++) pvar += (am[i]-pm)*(am[i]-pm);
    float jerk = 0; for (int i = 1; i < FD_WIN; i++) { float j = fabsf(am[i]-am[i-1]); if (j > jerk) jerk = j; }
    float gpeak = 0, gpre = 0, gpost = 0, glate = 0;
    for (int i = 0; i < FD_WIN; i++) { if (gm[i] > gpeak) gpeak = gm[i]; if (i < p) gpre += gm[i]; else gpost += gm[i]; if (i >= post0) glate += gm[i]; }
    int h = FD_FS/2; float ab[3] = {0,0,0}, aa[3] = {0,0,0};
    for (int i = 0; i < h; i++) for (int k = 0; k < 3; k++) { ab[k] += w[6*i+k]; aa[k] += w[6*(FD_WIN-h+i)+k]; }
    for (int k = 0; k < 3; k++) { ab[k] /= h; aa[k] /= h; }
    float c = (ab[0]*aa[0]+ab[1]*aa[1]+ab[2]*aa[2]) / (mag3(ab)*mag3(aa) + 1e-9f);
    if (c > 1) c = 1; if (c < -1) c = -1;
    int hi = 0; for (int i = 0; i < FD_WIN; i++) if (am[i] > 1.8f) hi++;
    o[0]=peak; o[1]=pre_min; o[2]=sqrtf(pre_var/p); o[3]=pre_max; o[4]=pdev/np; o[5]=sqrtf(pvar/np); o[6]=pmax;
    o[7]=jerk*FD_FS; o[8]=gpeak; o[9]=gpre/p; o[10]=gpost/(FD_WIN-p); o[11]=glate/np;
    o[12]=acosf(c)*57.29578f; o[13]=(float)hi/FD_FS;
}

float fd_predict(const float *f) {
    float s = 0;
    for (int t = 0; t < FM_N_TREES; t++) {
        uint16_t n = FM_ROOT[t];
        while (FM_FEAT[n] >= 0) n = (f[FM_FEAT[n]] <= FM_THR[n]) ? FM_LEFT[n] : FM_RIGHT[n];
        s += FM_VAL[n];
    }
    return s / FM_N_TREES;
}

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
