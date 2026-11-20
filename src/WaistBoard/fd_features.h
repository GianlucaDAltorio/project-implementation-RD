/* Window features shared by the watch and the waist board (plain C).
 *
 * fd_features() turns a 3 s IMU window (1 s before to 2 s after the impact peak, g and
 * deg/s) into the 14 features of src/fall_model/wrist_windows.py. fd_height_change()
 * gives the height change across the impact from barometer samples, as in
 * src/fall_model/fusion_windows.py. The waist board keeps an identical copy of this file. */
#pragma once
#include <stdint.h>
#ifndef FD_FS
#define FD_FS 238            /* sample rate in Hz, must match the IMU configuration */
#endif
#define FD_PRE   (FD_FS)             /* 1.0 s before impact */
#define FD_POST  (2 * FD_FS)         /* 2.0 s after impact  */
#define FD_WIN   (FD_PRE + FD_POST)  /* 3.0 s window */
#define FD_N_FEAT 14
#define FD_ALIGN (FD_FS / 4)         /* waist window: centred on its own |a| peak within this many
                                        samples of the wrist peak, so link delay does not matter */

#ifdef __cplusplus
extern "C" {
#endif
/* w is FD_WIN rows of ax ay az gx gy gz, row-major */
void  fd_features(const float *w, float *out14);
/* t_s: sample times in seconds relative to the impact; p_hpa: pressures. Returns metres,
 * negative = lower after the impact, or NAN if either side has no samples. */
float fd_height_change(const float *t_s, const float *p_hpa, int n);
#ifdef __cplusplus
}
#endif
