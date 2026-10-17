/* detmath.c - the transcendental functions, the same in every build.
 *
 * The engine (its sine tables, its menus' 3D), libvorbis (its transforms'
 * tables, its floors) and Mesa (its lighting, its texture levels) call sin,
 * cos, tan, the arc functions, exp, log, pow and their base-2 kin. The
 * C library's versions are accurate, but not to the same last bit in glibc
 * (the native reference) and musl (the sandbox), and a last bit is enough to
 * change a table entry or a decoded sample and split the two builds. The link
 * wraps each of them (sources.mk) and sends it here: computed from + - * /
 * and exact operations only (IEEE double: the same result on every x86-64),
 * with ordinary range reduction and series - plenty for a game, and identical
 * everywhere. The compiler is told they are not builtins, so it neither
 * folds nor fuses them (sin and cos of one angle into a sincos call that
 * would miss the wrap).
 *
 * The kernels are the rawgl core's detmath.h; the arc functions and the
 * base-2 and base-10 ones are added.
 */
#include <math.h>

static const double DM_PI = 3.14159265358979323846;
static const double DM_LN2 = 0.69314718055994530942;
static const double DM_LN10 = 2.30258509299404568402;

/* sin and cos on [-pi/4, pi/4]: Taylor series to x^17 (error below 1e-17) */
static double dm_sin_kernel(double x)
{
	const double x2 = x * x;
	double t = x, s = x;
	for (int n = 2; n <= 16; n += 2)
	{
		t = -t * x2 / (double)(n * (n + 1));
		s += t;
	}
	return s;
}

static double dm_cos_kernel(double x)
{
	const double x2 = x * x;
	double t = 1.0, s = 1.0;
	for (int n = 1; n <= 16; n += 2)
	{
		t = -t * x2 / (double)(n * (n + 1));
		s += t;
	}
	return s;
}

/* x = k * pi/2 + r, |r| <= pi/4 (Cody-Waite, pi/2 in three parts) */
static int dm_reduce(double x, double *r)
{
	static const double PIO2_1 = 1.57079632673412561417e+00;
	static const double PIO2_2 = 6.07710050650619224932e-11;
	static const double PIO2_3 = 2.02226624879595063154e-21;
	const double k = floor(x / (DM_PI / 2) + 0.5);
	*r = ((x - k * PIO2_1) - k * PIO2_2) - k * PIO2_3;
	return (int)((long long)k & 3);
}

static double dm_sin(double x)
{
	double r;
	switch (dm_reduce(x, &r))
	{
	case 0: return dm_sin_kernel(r);
	case 1: return dm_cos_kernel(r);
	case 2: return -dm_sin_kernel(r);
	default: return -dm_cos_kernel(r);
	}
}

static double dm_cos(double x)
{
	double r;
	switch (dm_reduce(x, &r))
	{
	case 0: return dm_cos_kernel(r);
	case 1: return -dm_sin_kernel(r);
	case 2: return -dm_cos_kernel(r);
	default: return dm_sin_kernel(r);
	}
}

static double dm_tan(double x) { return dm_sin(x) / dm_cos(x); }

/* e^x = 2^k * e^r, |r| <= ln2/2: Taylor series to r^20 */
static double dm_exp(double x)
{
	if (x != x) return x;
	if (x > 709.0) return HUGE_VAL;
	if (x < -745.0) return 0.0;
	const double k = floor(x / DM_LN2 + 0.5);
	const double r = (x - k * 6.93147180369123816490e-01) - k * 1.90821492927058770002e-10;
	double t = 1.0, s = 1.0;
	for (int n = 1; n <= 20; n++)
	{
		t = t * r / (double)n;
		s += t;
	}
	return ldexp(s, (int)k);
}

/* ln x = e * ln2 + 2 atanh((m - 1) / (m + 1)), m in [sqrt(1/2), sqrt(2)) */
static double dm_log(double x)
{
	if (x != x || x < 0.0) return NAN;
	if (x == 0.0) return -HUGE_VAL;
	if (x == HUGE_VAL) return x;
	int e;
	double m = frexp(x, &e); /* m in [0.5, 1) */
	if (m < 0.70710678118654752440)
	{
		m *= 2.0;
		e -= 1;
	}
	const double z = (m - 1.0) / (m + 1.0), z2 = z * z;
	double t = z, s = z;
	for (int n = 3; n <= 41; n += 2)
	{
		t *= z2;
		s += t / (double)n;
	}
	return (double)e * 6.93147180369123816490e-01 + ((double)e * 1.90821492927058770002e-10 + 2.0 * s);
}

static double dm_log10(double x) { return dm_log(x) / DM_LN10; }

static double dm_pow(double x, double y)
{
	if (y == 0.0) return 1.0;
	if (x == 1.0) return 1.0;
	if (x == 0.0) return y > 0.0 ? 0.0 : HUGE_VAL;
	if (x < 0.0)
	{
		/* only an integer power of a negative number is real */
		if (floor(y) != y) return NAN;
		const double v = dm_exp(y * dm_log(-x));
		return fmod(y, 2.0) != 0.0 ? -v : v;
	}
	return dm_exp(y * dm_log(x));
}

/* atan on |x| <= tan(pi/24) after two halvings: atan x = 2 atan(x / (1 + sqrt(1 + x^2))) */
static double dm_atan(double x)
{
	if (x != x) return x;
	const int neg = x < 0.0;
	if (neg) x = -x;
	double base = 0.0;
	if (x > 1.0)
	{
		base = DM_PI / 2;
		x = -1.0 / x;
	}
	x = x / (1.0 + sqrt(1.0 + x * x));
	x = x / (1.0 + sqrt(1.0 + x * x));
	const double x2 = x * x;
	double t = x, s = x;
	for (int n = 3; n <= 31; n += 2)
	{
		t = -t * x2;
		s += t / (double)n;
	}
	const double r = base + 4.0 * s;
	return neg ? -r : r;
}

static double dm_atan2(double y, double x)
{
	if (x != x || y != y) return x + y;
	if (x > 0.0) return dm_atan(y / x);
	if (x < 0.0) return y >= 0.0 ? dm_atan(y / x) + DM_PI : dm_atan(y / x) - DM_PI;
	if (y > 0.0) return DM_PI / 2;
	if (y < 0.0) return -DM_PI / 2;
	return 0.0;
}

static double dm_acos(double x)
{
	if (x != x || x > 1.0 || x < -1.0) return NAN;
	return dm_atan2(sqrt((1.0 - x) * (1.0 + x)), x);
}

static double dm_asin(double x)
{
	if (x != x || x > 1.0 || x < -1.0) return NAN;
	return dm_atan2(x, sqrt((1.0 - x) * (1.0 + x)));
}

double __wrap_sin(double x) { return dm_sin(x); }
double __wrap_cos(double x) { return dm_cos(x); }
double __wrap_tan(double x) { return dm_tan(x); }
double __wrap_acos(double x) { return dm_acos(x); }
double __wrap_atan(double x) { return dm_atan(x); }
double __wrap_atan2(double y, double x) { return dm_atan2(y, x); }
double __wrap_exp(double x) { return dm_exp(x); }
double __wrap_log(double x) { return dm_log(x); }
double __wrap_pow(double x, double y) { return dm_pow(x, y); }
double __wrap_exp2(double x) { return dm_exp(x * DM_LN2); }
double __wrap_log2(double x) { return dm_log(x) / DM_LN2; }
double __wrap_log10(double x) { return dm_log10(x); }
float __wrap_sinf(float x) { return (float)dm_sin(x); }
float __wrap_cosf(float x) { return (float)dm_cos(x); }
float __wrap_tanf(float x) { return (float)dm_tan(x); }
float __wrap_acosf(float x) { return (float)dm_acos(x); }
float __wrap_asinf(float x) { return (float)dm_asin(x); }
float __wrap_atanf(float x) { return (float)dm_atan(x); }
float __wrap_atan2f(float y, float x) { return (float)dm_atan2(y, x); }
float __wrap_expf(float x) { return (float)dm_exp(x); }
float __wrap_logf(float x) { return (float)dm_log(x); }
float __wrap_powf(float x, float y) { return (float)dm_pow(x, y); }
float __wrap_exp2f(float x) { return (float)dm_exp((double)x * DM_LN2); }
float __wrap_log2f(float x) { return (float)(dm_log(x) / DM_LN2); }
float __wrap_log10f(float x) { return (float)dm_log10(x); }
