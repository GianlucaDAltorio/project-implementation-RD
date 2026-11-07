/* Streaming wrist fall detector (portable C99, no Arduino or ESP-IDF dependencies).
 *
 * Call fd_push() once per IMU sample at FD_FS Hz, acceleration in g and rotation in deg/s.
 * An impact at or above FD_TRIG_G opens a window of 1 s before and 2 s after the peak;
 * the 14 window features go through the random forest in fall_model.h. fd_push() returns 1
 * when the window's fall probability reaches the model threshold, about 2 s after the impact.
 *
 * Feature definitions mirror src/fall_model/wrist_windows.py. */
#pragma once
#include <stdint.h>
#ifndef FD_FS
#define FD_FS 238            /* sample rate in Hz, must match the IMU configuration */
#endif
#define FD_PRE   (FD_FS)             /* 1.0 s before impact */
#define FD_POST  (2 * FD_FS)         /* 2.0 s after impact  */
#define FD_WIN   (FD_PRE + FD_POST)  /* 3.0 s window */
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
/* exposed for testing; the window is FD_WIN x 6, row-major */
void  fd_features(const float *w, float *out14);
float fd_predict(const float *feat14);
float fd_threshold(void);   /* decision threshold on the fall probability */
