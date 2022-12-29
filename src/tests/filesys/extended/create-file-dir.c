#include "tests/lib.h"
#include "tests/main.h"
#include <stdio.h>
#include <syscall.h>

void
test_main (void)
{
  CHECK (!create ("..", 0), "create .. must fail");
  CHECK (!create (".", 0), "create . must fail");
  CHECK (!create ("a", 0), "create a ok");
  CHECK (!create ("b//", 0), "create b// fail");
}
