/* PC-only test program for the fused model.
 *
 *   harness_fusion <dir> <trial> <idx>   prints the fused fall probability and the 29
 *                                        features for the window whose wrist impact is at
 *                                        sample <idx>
 *
 * <trial> is the FallAllD stem without the device, e.g. S01_A101_T01; the wrist (D2) and
 * waist (D3) files are read from <dir>. Build and run through fusion_parity.py.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fall_detect.h"

static int load3(const char *path, float scale, float (*x)[6], int col) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256]; int i = 0;
    while (fgets(line, sizeof line, f) && i < 5000) {
        double a, b, c;
        sscanf(line, "%lf,%lf,%lf", &a, &b, &c);
        x[i][col] = a * scale; x[i][col + 1] = b * scale; x[i][col + 2] = c * scale;
        i++;
    }
    fclose(f);
    return i;
}

static int load_device(const char *dir, const char *trial, int dev, float (*x)[6]) {
    char s[8], a[8], t[8], p[512];
    sscanf(trial, "%7[^_]_%7[^_]_%7s", s, a, t);
    snprintf(p, sizeof p, "%s/%s_D%d_%s_%s_A.dat", dir, s, dev, a, t);
    int n = load3(p, 0.000244f, x, 0);
    snprintf(p, sizeof p, "%s/%s_D%d_%s_%s_G.dat", dir, s, dev, a, t);
    load3(p, 0.07f, x, 3);
    return n;
}

int main(int argc, char **argv) {
    static float wr[5000][6], wa[5000][6], win[FD_WIN * 6], t_s[400], p_hpa[400];
    if (argc < 4) return 1;
    const char *dir = argv[1], *trial = argv[2];
    int idx = atoi(argv[3]);
    load_device(dir, trial, 2, wr);
    load_device(dir, trial, 3, wa);

    float w14[14], z15[15];
    for (int k = 0; k < FD_WIN; k++) memcpy(win + 6 * k, wr[idx - FD_PRE + k], 24);
    fd_features(win, w14);
    int j = idx - FD_ALIGN; float best = -1;          /* waist's own peak near the wrist peak */
    for (int k = idx - FD_ALIGN; k <= idx + FD_ALIGN; k++) {
        float m = sqrtf(wa[k][0] * wa[k][0] + wa[k][1] * wa[k][1] + wa[k][2] * wa[k][2]);
        if (m > best) { best = m; j = k; }
    }
    for (int k = 0; k < FD_WIN; k++) memcpy(win + 6 * k, wa[j - FD_PRE + k], 24);
    fd_features(win, z15);

    char s[8], a[8], t[8], p[512], line[128];
    sscanf(trial, "%7[^_]_%7[^_]_%7s", s, a, t);
    snprintf(p, sizeof p, "%s/%s_D3_%s_%s_B.dat", dir, s, a, t);
    FILE *f = fopen(p, "r");
    int n = 0;
    while (f && fgets(line, sizeof line, f) && n < 400) {
        double pr, tc;
        sscanf(line, "%lf,%lf", &pr, &tc);
        p_hpa[n] = (float)pr; t_s[n] = n / 10.0f - j / (float)FD_FS; n++;
    }
    if (f) fclose(f);
    z15[14] = fd_height_change(t_s, p_hpa, n);

    printf("%.6f", fd_predict_fusion(w14, z15));
    for (int i = 0; i < 14; i++) printf(",%.6f", w14[i]);
    for (int i = 0; i < 15; i++) printf(",%.6f", z15[i]);
    printf("\n");
    return 0;
}
