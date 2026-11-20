/* See fd_features.h. */
#include "fd_features.h"
#include <math.h>
#include <string.h>

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

static float median(float *x, int n) {        /* sorts x in place */
    for (int i = 1; i < n; i++) { float v = x[i]; int j = i - 1; while (j >= 0 && x[j] > v) { x[j + 1] = x[j]; j--; } x[j + 1] = v; }
    return (n % 2) ? x[n / 2] : 0.5f * (x[n / 2 - 1] + x[n / 2]);
}

static double height_m(double p) { return 44330.0 * (1.0 - pow(p / 1013.25, 0.1903)); }

float fd_height_change(const float *t_s, const float *p_hpa, int n) {
    static float before[256], after[256];
    int nb = 0, na = 0;
    for (int i = 0; i < n; i++) {
        if (t_s[i] >= -2.0f && t_s[i] < -0.5f && nb < 256) before[nb++] = p_hpa[i];
        if (t_s[i] >= 1.0f && t_s[i] < 2.0f && na < 256) after[na++] = p_hpa[i];
    }
    if (!nb || !na) return NAN;
    return (float)(height_m(median(after, na)) - height_m(median(before, nb)));
}
