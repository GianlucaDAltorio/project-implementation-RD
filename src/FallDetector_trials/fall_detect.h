/* Streaming wrist fall detector (plain C, no Arduino dependencies).
 *
 * Call fd_push() for every IMU sample (g and deg/s). An impact of 2.5 g or more opens a
 * window from 1 s before to 2 s after the peak, and the 14 window features go through the
 * wrist random forest in fall_model.h. fd_push() returns 1 when the window is classed as a
 * fall by the wrist model alone. fd_predict_fusion() scores the same window together with
 * the waist board's 15 features, using fall_model_fusion.h. */
#pragma once
#include <stdint.h>
#include "fd_features.h"
#define FD_TRIG_G 2.5f               /* impact trigger, total acceleration in g */

typedef struct {
    float buf[FD_WIN][6];     /* ring buffer: ax ay az gx gy gz */
    uint32_t n;               /* samples seen so far */
    uint8_t  state;           /* 0 idle, 1 locking peak, 2 waiting for post-impact data */
    uint32_t cross_n, peak_n; float peak_g;
    float last_prob;          /* probability of the last evaluated window */
    float last_peak_g;        /* impact peak (g) of the last evaluated window */
    uint32_t n_eval;          /* number of windows evaluated so far */
    float last_feat[14];      /* the 14 features of the last evaluated window */
} fd_t;

void  fd_init(fd_t *d);
int   fd_push(fd_t *d, float ax, float ay, float az, float gx, float gy, float gz);
float fd_predict(const float *feat14);
float fd_threshold(void);   /* decision threshold on the fall probability */
/* waist15 = the waist board's 14 features followed by its height change in metres */
float fd_predict_fusion(const float *wrist14, const float *waist15);
float fd_threshold_fusion(void);
