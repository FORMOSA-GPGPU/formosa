// Q16.16 fixed-point Mandelbrot

#define FP_FRAC 16
#define FP_ONE  (1 << FP_FRAC)

static inline int qmul(int a, int b) {
  long v = (long)a * (long)b;
  return (int)(v >> FP_FRAC);
}

static inline int qsqr(int a) { return qmul(a, a); }

// a fast-checking function to check a point must inside mandelbrot set
static inline int inside_fast_q16(int cx, int cy) {

  // period-2 bulb: (x+1)^2 + y^2 <= (1/4)^2

  int xp1 = cx + FP_ONE;
  int left2 = qsqr(xp1) + qsqr(cy);
  if (left2 <= (FP_ONE >> 4)) return 1;

  // cardioid check:
  // let q = (x - 1/4)^2, check if q(q + (x - 1/4)) <= y^2 / 4

  int xmquarter = cx - (FP_ONE >> 2);         // x - 1/4
  int q = qsqr(xmquarter) + qsqr(cy);         // q
  int left = qmul(q, q + xmquarter); // q(q + (x - 1/4))
  int right = qsqr(cy) >> 2;
  return left <= right;
}

// calc mandelbrot in Q16.16 fix point
__kernel void mandelbrot_fix_point(__global uchar* out_img, const int width,
                                   const int height, const int xmin_q,
                                   const int ymin_q, const int dx_q,
                                   const int dy_q, const int max_iter) {
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if (x >= width || y >= height) return;

  const int idx = y * width + x;

  // complex plane, x for real part, y for imagine part
  int cx = xmin_q + (int)(((long)x * (long)dx_q));
  int cy = ymin_q + (int)(((long)y * (long)dy_q));
  if (inside_fast_q16(cx, cy)) { out_img[idx] = 0; return; }

  // Z_{n+1} = Z_n^2 + C
  // Real(Z) = zx
  // Imag(Z) = zy
  int zx = 0, zy = 0;
  int zx2 = 0, zy2 = 0;

  int iter = 0;
  const int escape_radius_square = (4 << FP_FRAC);

  while (iter < max_iter) {
    zx2 = qsqr(zx);
    zy2 = qsqr(zy);
    if((zx2 + zy2) > escape_radius_square)
      break;
    int zxzy = qmul(zx, zy);
    // Z_{n+1} = Z_n^2 + C
    zy = (zxzy << 1) + cy;
    zx = (zx2 - zy2) + cx;
    ++iter;
  }

  // only draw result inside iters
  uchar val = (iter < max_iter) * 255;
  out_img[idx] = val;
}
