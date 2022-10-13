#ifndef FIXED_POINT_H
#define FIXED_POINT_H
typedef signed int fp14;

// Convert n to fixed point: n * f
#define int_to_fp14(n) ((n) << 14)
// Convert x to interger: x / f
#define fp14_to_int_trunc(x) ((x) >> 14)
// convert x to interger (rounding to nearest):
// (x + f / 2) / f if x >= 0
// (x - f / 2) /f if x <= 0
#define fp14_to_int_round(x)                                                  \
  ((x) >= 0 ? (((x) + (1 << 13)) >> 14) : (((x) - (1 << 13)) >> 14))
// Add x and y: x + y
#define fp14_add_fp14(x, y) ((x) + (y))
// Subtract y from x
#define fp14_sub_fp14(x, y) ((x) - (y))
// Add x and n
#define fp14_add_int(x, n) ((x) + ((n) << 14))
// Subtract n from x
#define fp14_sub_int(x, n) ((x) - ((n) << 14))
// Multiply x by y
#define fp14_mul_fp14(x, y) ((((int64_t)(x)) * (y)) >> 14)
// Multiply x by n
#define fp14_mul_int(x, n) ((x) * (n))
// Divide x by y
#define fp14_div_fp14(x, y) ((((int64_t)(x)) << 14) / (y))
// Divide x by n
#define fp14_div_int(x, n) ((x) / (n))
#endif // FIXED_POINT_H