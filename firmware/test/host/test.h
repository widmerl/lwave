// Minimal test framework for the host build (no external dependencies).
#pragma once
#include <math.h>
#include <stdio.h>

namespace lwtest {

/// A registered test case.
struct Case {
  const char* name;
  void (*fn)();
  Case* next;
};

/// Head of the registered list.
Case*& registry();
/// Number of failed checks so far.
int& failures();

/// Registers a test at static-initialisation time.
struct Registrar {
  Registrar(const char* name, void (*fn)(), Case* storage) {
    storage->name = name;
    storage->fn = fn;
    storage->next = registry();
    registry() = storage;
  }
};

}  // namespace lwtest

/// Defines a test case.
#define TEST(name)                                                                  \
  static void name();                                                               \
  static lwtest::Case name##_case;                                                  \
  static lwtest::Registrar name##_reg(#name, name, &name##_case);                   \
  static void name()

/// Fails (and continues) if cond is false.
#define CHECK(cond)                                                                 \
  do {                                                                              \
    if (!(cond)) {                                                                  \
      ++lwtest::failures();                                                         \
      printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                      \
    }                                                                               \
  } while (0)

/// Fails if |a - b| > tol.
#define CHECK_NEAR(a, b, tol)                                                       \
  do {                                                                              \
    double a_ = (a), b_ = (b);                                                      \
    if (!(fabs(a_ - b_) <= (tol))) {                                                \
      ++lwtest::failures();                                                         \
      printf("  FAIL %s:%d: %s = %g, expected %g ± %g\n", __FILE__, __LINE__, #a, a_, b_, (double)(tol)); \
    }                                                                               \
  } while (0)
