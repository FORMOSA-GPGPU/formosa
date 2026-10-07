#include <CL/opencl.hpp>
#include <fstream>
#include <iostream>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

#define FP_FRAC (16)  // Q16.16
#define FP_ONE (1 << FP_FRAC)

inline int qmul(int a, int b) {
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

  int xmquarter = cx - (FP_ONE >> 2);  // x - 1/4
  int q = qsqr(xmquarter) + qsqr(cy);  // q
  int left = qmul(q, q + xmquarter);   // q(q + (x - 1/4))
  int right = qsqr(cy) >> 2;
  return left <= right;
}

static void write_img(const char *path, int w, int h, unsigned char *img) {
  FILE *f = fopen(path, "wb");
  if (!f) {
    perror("fopen");
    exit(1);
  }
  fprintf(f, "P5\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; ++i) {
    fwrite(&img[i], 1, 1, f);
  }
  fclose(f);
}

static void cl_sanity_check(cl_int err, const char *where) {
  if (err != CL_SUCCESS) {
    fprintf(stderr, "OpenCL error %d at %s\n", err, where);
    exit(1);
  }
}

void mandelbrot_cpu(const int height, const int width, const int xmin_q,
                    const int ymin_q, const int dx_q, const int dy_q,
                    const int max_iter, std::vector<uint8_t> &golden) {
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int idx = y * width + x;
      int cx = xmin_q + (int)(((long)x * (long)dx_q));
      int cy = ymin_q + (int)(((long)y * (long)dy_q));
      if (inside_fast_q16(cx, cy)) {
        golden[idx] = 0;
        continue;
      }

      // zx: real part
      // zy: imaginary part
      int zx = 0, zy = 0;
      int zx2 = 0, zy2 = 0;

      int iter = 0;
      const int escape_radius_square = (4 << FP_FRAC);

      while (iter < max_iter) {
        zx2 = qsqr(zx);
        zy2 = qsqr(zy);
        if ((zx2 + zy2) > escape_radius_square) break;
        int zxzy = qmul(zx, zy);
        // Z_{n+1} = Z_n^2 + C
        zy = (zxzy << 1) + cy;
        zx = (zx2 - zy2) + cx;
        ++iter;
      }
      golden[idx] = (iter < max_iter) ? 255 : 0;
    }
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <width> [outName.ppm]\n", argv[0]);
    return 1;
  }

  const int width = atoi(argv[1]);
  const int height = width * 3 / 4;
  std::vector<cl::Platform> platforms;
  cl::Platform::get(&platforms);
  if (platforms.empty()) {
    std::cerr << "No platforms!" << std::endl;
    return -1;
  }

  cl::Platform platform = platforms[0];
  std::vector<cl::Device> devices;

  platform.getDevices(CL_DEVICE_TYPE_GPU, &devices);
  if (devices.empty()) {
    std::cerr << "No Devices!" << std::endl;
    return -1;
  }

  cl::Device device = devices[0];
  cl::Context context({device});
  cl::CommandQueue queue(context, device);

  std::ifstream t(KERNEL_PATH);
  if (!t) {
    std::cerr << "Error Opening Kernel Source file\n";
    return -1;
  }

  std::string source = {std::istreambuf_iterator<char>(t),
                        std::istreambuf_iterator<char>()};

  cl::Program::Sources sources = {source};

  cl::Program program(context, sources);
  if (program.build() != CL_SUCCESS) {
    std::cerr << "Fail to build" << std::endl;
    return -1;
  }
  cl::Kernel mandelbrot_fix_point(program, "mandelbrot_fix_point");
  size_t npix = (size_t)width * (size_t)height;
  cl::Buffer result_buf(context, CL_MEM_WRITE_ONLY, sizeof(uint8_t) * npix);
  std::vector<uint8_t> result(npix);
  std::vector<uint8_t> golden(npix);

  // Complex plane viewport
  const double center_x = -0.75, center_y = 0.0;
  const double span_x = 3.0;
  const double span_y = span_x * (double)height / (double)width;
  const double xmin = center_x - span_x * 0.5;
  const double ymin = center_y - span_y * 0.5;
  const double dx = span_x / (double)(width - 1);
  const double dy = span_y / (double)(height - 1);
  const double q_fact = (double)(1 << 16);
  int xmin_q = (int)(xmin * q_fact);
  int ymin_q = (int)(ymin * q_fact);
  int dx_q = (int)(dx * q_fact);
  int dy_q = (int)(dy * q_fact);
  const int max_iter = 1024;
  mandelbrot_fix_point.setArg(0, result_buf);
  mandelbrot_fix_point.setArg(1, width);
  mandelbrot_fix_point.setArg(2, height);
  mandelbrot_fix_point.setArg(3, xmin_q);
  mandelbrot_fix_point.setArg(4, ymin_q);
  mandelbrot_fix_point.setArg(5, dx_q);
  mandelbrot_fix_point.setArg(6, dy_q);
  mandelbrot_fix_point.setArg(7, max_iter);

  cl_int err =
      queue.enqueueNDRangeKernel(mandelbrot_fix_point, cl::NullRange,
                                 cl::NDRange(width, height), cl::NullRange);
  cl_sanity_check(err, "enq NDRange");

  err = queue.enqueueReadBuffer(result_buf, CL_FALSE, 0, sizeof(uint8_t) * npix,
                                result.data());
  cl_sanity_check(err, "enq ReadBuf");

  mandelbrot_cpu(height, width, xmin_q, ymin_q, dx_q, dy_q, max_iter, golden);

  queue.finish();

  if (golden != result) {
    std::cout << "Failed!! GPU data is mismatched with golden\n";
    return -1;
  }

  if (argc == 3) {
    std::string out_path = argv[2];
    if (out_path.size() < 4 ||
        out_path.compare(out_path.size() - 4, 4, ".ppm") != 0)
      out_path += ".ppm";
    write_img(out_path.c_str(), width, height, result.data());
    std::cout << "Wrote img " << out_path << " (" << width << "x" << height
              << ")\n";
  }
  return 0;
}
