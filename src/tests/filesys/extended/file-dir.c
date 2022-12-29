#include "tests/lib.h"
#include "tests/main.h"
#include <stdio.h>
#include <syscall.h>

void
test_main (void)
{
  CHECK (!create ("..", 0), "create .. must fail");
  CHECK (!create (".", 0), "create . must fail");
  CHECK (!create ("a/", 0), "create a/ fail");
  CHECK (!create ("b//", 0), "create b// fail");

  CHECK (mkdir ("hello"), "mkdir hello ok");
  CHECK (!mkdir ("hello/./"), "mkdir hello/./ should fail");

  CHECK (create ("hello/a", 0), "create hello/a ok");
  CHECK (!create ("hello/./a", 0), "create hello/./a fail");
}
