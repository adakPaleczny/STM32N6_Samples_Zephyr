#include <math.h>

/*
 * libneai.a was compiled against the ARM toolchain's libm, which exposes
 * expf() under the hard-float ABI mangled name __hardfp_expf.
 * Zephyr's picolibc exports the same function as plain expf().
 * This shim makes the linker happy.
 */
float __hardfp_expf(float x) { return expf(x); }
