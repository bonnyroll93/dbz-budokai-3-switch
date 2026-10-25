/*
 * ps3recomp - deko3d RSX backend for Nintendo Switch (T22)
 *
 * Drives the register-file draw engine (rsx_draw_engine.h) on the Switch GPU
 * through deko3d. It handles colour and depth targets, clears, draws,
 * presentation, readback, and CPU writes for 2D transfer coherence.
 */
#ifndef PS3RECOMP_RSX_DEKO3D_BACKEND_H
#define PS3RECOMP_RSX_DEKO3D_BACKEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Non-zero by default. PS3RECOMP_RSX_GPU=0 selects the software path. */
int  rsx_deko3d_backend_requested(void);

/* 0 on success. On failure everything is torn down again, the native window
 * is free, and the caller can fall back to the software backend. */
int  rsx_deko3d_backend_init(uint32_t width, uint32_t height, const char* title);
void rsx_deko3d_backend_shutdown(void);
void rsx_deko3d_backend_present(void);
int  rsx_deko3d_backend_pump_messages(void);

#ifdef __cplusplus
}
#endif
#endif /* PS3RECOMP_RSX_DEKO3D_BACKEND_H */
