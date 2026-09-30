/* The pinned LLVM emits C23 numeric min/max calls. The tg5040 sysroot
 * predates those symbols, so retain their NaN-suppressing semantics. */
#include <math.h>
float fmaximum_numf(float a, float b) { return fmaxf(a, b); }
float fminimum_numf(float a, float b) { return fminf(a, b); }
double fmaximum_num(double a, double b) { return fmax(a, b); }
double fminimum_num(double a, double b) { return fmin(a, b); }
