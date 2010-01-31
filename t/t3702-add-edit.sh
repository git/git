#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='add -e basic tests'
. ./test-lib.sh


cat > file << EOF
LO, praise of the prowess of people-kings
of spear-armed Danes, in days long sped,
we have heard, and what honor the athelings won!
Oft Scyld the Scefing from squadroned foes,
from many a tribe, the mead-bench tore,
awing the earls. Since erst he lay
friendless, a foundling, fate repaid him:
for he waxed under welkin, in wealth he throve,
till before him the folk, both far and near,
who house by the whale-path, heard his mandate,
gave him gifts:  a good king he!
EOF

cat > second-part << EOF
To him an heir was afterward born,
a son in his halls, whom heaven sent
to favor the folk, feeling their woe
that erst they had lacked an earl for leader
so long a while; the Lord endowed him,
the Wielder of Wonder, with world's renown.
EOF

test_expect_success 'setup' '

	git add file &&
	test_tick &&
	git commit -m initial file

'

cat > expected-patch << EOF
diff --git a/file b/file
index b9834b5..9020acb 100644
--- a/file
+++ b/file
@@ -1,11 +1,6 @@
-LO, praise of the prowess of people-kings
-of spear-armed Danes, in days long sped,
-we have heard, and what honor the athelings won!
-Oft Scyld the Scefing from squadroned foes,
-from many a tribe, the mead-bench tore,
-awing the earls. Since erst he lay
-friendless, a foundling, fate repaid him:
-for he waxed under welkin, in wealth he throve,
-till before him the folk, both far and near,
-who house by the whale-path, heard his mandate,
-gave him gifts:  a good king he!
+To him an heir was afterward born,
+a son in his halls, whom heaven sent
+to favor the folk, feeling their woe
+that erst they had lacked an earl for leader
+so long a while; the Lord endowed him,
+the Wielder of Wonder, with world's renown.
EOF

cat > patch << EOF
diff --git a/file b/file
index b9834b5..ef6e94c 100644
--- a/file
+++ b/file
@@ -3,1 +3,333 @@ of spear-armed Danes, in days long sped,
 we have heard, and what honor the athelings won!
+
 Oft Scyld the Scefing from squadroned foes,
@@ -2,7 +1,5 @@ awing the earls. Since erst he lay
 friendless, a foundling, fate repaid him:
+
 for he waxed under welkin, in wealth he throve,
EOF

cat > expected << EOF
diff --git a/file b/file
index b9834b5..ef6e94c 100644
--- a/file
+++ b/file
@@ -1,10 +1,12 @@
 LO, praise of the prowess of people-kings
 of spear-armed Danes, in days long sped,
 we have heard, and what honor the athelings won!
+
 Oft Scyld the Scefing from squadroned foes,
 from many a tribe, the mead-bench tore,
 awing the earls. Since erst he lay
 friendless, a foundling, fate repaid him:
+
 for he waxed under welkin, in wealth he throve,
 till before him the folk, both far and near,
 who house by the whale-path, heard his mandate,
EOF

echo "#!$SHELL_PATH" >fake-editor.sh
cat >> fake-editor.sh <<\EOF
mv -f "$1" orig-patch &&
mv -f patch "$1"
EOF

test_set_editor "$(pwd)/fake-editor.sh"
chmod a+x fake-editor.sh

test_expect_success 'add -e' '

	cp second-part file &&
	git add -e &&
	test_cmp second-part file &&
	test_cmp orig-patch expected-patch &&
	git diff --cached > out &&
	test_cmp out expected

'

test_done
