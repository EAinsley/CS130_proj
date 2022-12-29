# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(create-file-dir) begin
(create-file-dir) create .. must fail
(create-file-dir) create . must fail
(create-file-dir) create a ok
(create-file-dir) create b// fail
(create-file-dir) end
create-root: exit(0)
EOF
pass;
