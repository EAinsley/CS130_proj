# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(file-dir) begin
(file-dir) create .. must fail
(file-dir) create . must fail
(file-dir) create a/ fail
(file-dir) create b// fail
(file-dir) mkdir hello ok
(file-dir) mkdir hello/./ should fail
(file-dir) create hello/a ok
(file-dir) create hello/./a fail
(file-dir) end
file-dir: exit(0)
EOF
pass;
