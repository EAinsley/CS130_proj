#include "tests/lib.h"
#include "tests/main.h"
#include <stdio.h>
#include <syscall.h>

void
test_main (void)
{
  CHECK (!mkdir ("//"), "must false 1.1");
  CHECK (!mkdir ("//./"), "must false 1.2");
  CHECK (!mkdir ("//../"), "must false 1.3");
  CHECK (!mkdir ("//../.."), "must false 1.4");
  CHECK (mkdir ("a"), "mkdir /a/ ok");
  CHECK (mkdir ("/../../a/b"), "mkdir /a/b/ ok");
  CHECK(!mkdir("a/b/"), "make a/b/ must fail");
  CHECK (mkdir ("a/c/"), "make a/c/ ok");
}
