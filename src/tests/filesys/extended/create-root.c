#include "tests/lib.h"
#include "tests/main.h"
#include <stdio.h>
#include <syscall.h>

// CHECK (!mkdir (""), "mkdir \"\" (must return false)");

void
test_main (void)
{
  CHECK (!mkdir ("//"), "must false 1.1");
  CHECK (!mkdir ("//./"), "must false 1.2");
  CHECK (!mkdir ("//../"), "must false 1.3");
  CHECK (!mkdir ("//../.."), "must false 1.4");

  // CHECK (!create ("..", 0), "must fail 2.1");
  // CHECK (!create (".", 0), "must fail 2.2");
}