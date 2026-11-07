/* PC-only test program for the detector in src/FallDetector_trials.
 *
 *   harness parity <dir> <trial> <idx>   prints the fall probability and the 14 features
 *                                        for the window whose impact is at sample <idx>
 *   harness stream <dir> <trial>         runs the streaming detector over a whole trial
 *                                        and prints "<alarms>,<max probability>"
 *
 * <dir> holds the FallAllD .dat files; <trial> is the file stem, e.g. S01_D2_A101_T01.
 * Build and run through parity_check.py.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fall_detect.h"
#include "fall_model.h"

static int load(const char *dir, const char *base, float (*x)[6]) {
    const char *sf = "AG";
    const float sc[2] = {0.000244f, 0.07f};
    int n = 0;
    for (int s = 0; s < 2; s++) {
        char p[512];
        snprintf(p, sizeof p, "%s/%s_%c.dat", dir, base, sf[s]);
        FILE *f = fopen(p, "r");
        if (!f) return -1;
        char line[256];
        int i = 0;
        while (fgets(line, sizeof line, f) && i < 5000) {
            double a, b, c;
            sscanf(line, "%lf,%lf,%lf", &a, &b, &c);
            x[i][3*s] = a*sc[s]; x[i][3*s+1] = b*sc[s]; x[i][3*s+2] = c*sc[s];
            i++;
        }
        fclose(f);
        n = i;
    }
    return n;
}

int main(int argc, char **argv) {
    static float x[5000][6];
    if (argc < 4) return 1;
    const char *dir = argv[2];
    if (!strcmp(argv[1], "parity")) {
        load(dir, argv[3], x);
        int i = atoi(argv[4]);
        static float w[FD_WIN*6], f[14];
        for (int k = 0; k < FD_WIN; k++) memcpy(w + 6*k, x[i - FD_PRE + k], 24);
        fd_features(w, f);
        printf("%.6f", fd_predict(f));
        for (int k = 0; k < 14; k++) printf(",%.6f", f[k]);
        printf("\n");
    } else {
        int n = load(dir, argv[3], x);
        fd_t d;
        fd_init(&d);
        int alarms = 0;
        float pmax = 0;
        for (int i = 0; i < n; i++) {
            alarms += fd_push(&d, x[i][0], x[i][1], x[i][2], x[i][3], x[i][4], x[i][5]);
            if (d.last_prob > pmax) pmax = d.last_prob;
        }
        printf("%d,%.4f\n", alarms, pmax);
    }
    return 0;
}
