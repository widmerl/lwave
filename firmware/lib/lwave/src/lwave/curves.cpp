#include "lwave/curves.h"
#include <math.h>

namespace lw {

float curveLinear(float p, float lo, float hi) {
  p = clampf(p, 0.0f, 1.0f);
  return lo + (hi - lo) * p;
}

float curveLog3(float p, float lo, float mid, float hi) {
  p = clampf(p, 0.0f, 1.0f);
  if (p < 0.5f) return lo * powf(mid / lo, 2.0f * p);
  return mid * powf(hi / mid, 2.0f * p - 1.0f);
}

LogTo0 LogTo0::fit(float pMid, float vMid, float vHi) {
  // Solve (b - 1) / (b^pMid - 1) = vHi / vMid for b by bisection on log(b).
  const double target = (double)vHi / (double)vMid;
  double lo = 1e-6, hi = 60.0;  // range of ln(b)
  for (int i = 0; i < 200; ++i) {
    double m = 0.5 * (lo + hi);
    double b = exp(m);
    double r = (b - 1.0) / (pow(b, pMid) - 1.0);
    if (r < target) lo = m; else hi = m;
  }
  LogTo0 c;
  c.b = (float)exp(0.5 * (lo + hi));
  c.a = (float)(vHi / (c.b - 1.0));
  return c;
}

float LogTo0::operator()(float p) const {
  p = clampf(p, 0.0f, 1.0f);
  return a * (powf(b, p) - 1.0f);
}

float curveDbTo0(float p, float dbLo, float dbHi, float fadeEnd) {
  p = clampf(p, 0.0f, 1.0f);
  if (p <= 0.0f) return 0.0f;
  if (p < fadeEnd) {
    float edgeDb = dbLo + (dbHi - dbLo) * fadeEnd;
    return dbToGain(edgeDb) * (p / fadeEnd);
  }
  return dbToGain(dbLo + (dbHi - dbLo) * p);
}

float dbToGain(float db) { return powf(10.0f, db * 0.05f); }

float gainToDb(float gain) { return gain > 1e-6f ? 20.0f * log10f(gain) : -120.0f; }

}  // namespace lw
