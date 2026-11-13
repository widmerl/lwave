// Runs every registered test and reports the number of failed checks.
#include <string.h>
#include "test.h"

namespace lwtest {
Case*& registry() {
  static Case* head = nullptr;
  return head;
}
int& failures() {
  static int n = 0;
  return n;
}
}  // namespace lwtest

int main(int argc, char** argv) {
  // Reverse the list so tests run in file order.
  lwtest::Case* rev = nullptr;
  for (lwtest::Case* c = lwtest::registry(); c;) {
    lwtest::Case* next = c->next;
    c->next = rev;
    rev = c;
    c = next;
  }
  int run = 0;
  for (lwtest::Case* c = rev; c; c = c->next) {
    if (argc > 1 && !strstr(c->name, argv[1])) continue;
    int before = lwtest::failures();
    c->fn();
    printf("%s %s\n", lwtest::failures() == before ? "ok  " : "FAIL", c->name);
    ++run;
  }
  printf("\n%d tests, %d failed checks\n", run, lwtest::failures());
  return lwtest::failures() == 0 ? 0 : 1;
}
