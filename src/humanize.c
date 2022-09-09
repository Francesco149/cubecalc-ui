#ifndef HUMANIZE_H
#define HUMANIZE_H

#include <stdint.h>
#include <unistd.h>
typedef int_least64_t humanizeI64;

void humanize(char* buf, size_t sz, humanizeI64 value);

#endif

#ifdef HUMANIZE_IMPLEMENTATION
#include <stdio.h>
#include <inttypes.h>

int humanizeSnprintf(humanizeI64 x, char* buf, size_t sz, char const* suff) {
  return snprintf(buf, sz, "%" PRId64 "%s ", x, suff);
}

int humanizeSnprintfWithDot(double x, char* buf, size_t sz, char const* suff) {
  humanizeI64 mod = (humanizeI64)(x * 10) % 10;
  if (mod) {
    return snprintf(buf, sz, "%" PRId64 ".%" PRId64 "%s ", (humanizeI64)x, mod, suff);
  }
  return humanizeSnprintf((humanizeI64)x, buf, sz, suff);
}

int humanizeStepWithDot(humanizeI64 mag, char const* suff, char* buf, size_t sz, humanizeI64 value) {
  if (value >= mag) {
    double x = value / (double)mag;
    int n = humanizeSnprintfWithDot(x, 0, 0, suff);
    if (n >= sz) {
      snprintf(buf, sz, "...");
    } else {
      humanizeSnprintfWithDot(x, buf, sz, suff);
    }
    return n;
  }
  return 0;
}

void humanize(char* buf, size_t sz, humanizeI64 value) {
  const humanizeI64 k = 1000;
  const humanizeI64 m = k * k;
  const humanizeI64 b = k * m;
  const humanizeI64 t = k * b;
  const humanizeI64 q = k * t;

  if (value < 0) {
    int n = snprintf(buf, sz, "-");
    buf += n;
    sz -= n;
    value *= -1;
  }

  if (value < k) {
    snprintf(buf, sz, "%" PRId64, value);
    return;
  }

  if (value >= q * k) {
    if (snprintf(buf, sz, "%.2e", (double)value) >= sz) {
      snprintf(buf, sz, "(too big)");
    }
    return;
  }

  humanizeStepWithDot(q, "q", buf, sz, value) ||
  humanizeStepWithDot(t, "t", buf, sz, value) ||
  humanizeStepWithDot(b, "b", buf, sz, value) ||
  humanizeStepWithDot(m, "m", buf, sz, value) ||
  humanizeStepWithDot(k, "k", buf, sz, value);
}
#endif
