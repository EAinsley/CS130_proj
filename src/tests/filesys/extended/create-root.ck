# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(create-root) begin
(create-root) must false 1.1
(create-root) must false 1.2
(create-root) must false 1.3
(create-root) must false 1.4
(create-root) mkdir /a/ ok
(create-root) mkdir /a/b/ ok
(create-root) end
create-root: exit(0)
EOF
pass;
