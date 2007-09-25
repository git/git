#!/bin/sh

test_description='git-send-email'
. ./test-lib.sh

PROG='git send-email'
test_expect_success \
    'prepare reference tree' \
    'echo "1A quick brown fox jumps over the" >file &&
     echo "lazy dog" >>file &&
     git add file
     GIT_AUTHOR_NAME="A" git commit -a -m "Initial."'

test_expect_success \
    'Setup helper tool' \
    '(echo "#!/bin/sh"
      echo shift
      echo for a
      echo do
      echo "  echo \"!\$a!\""
      echo "done >commandline"
      echo "cat > msgtxt"
      ) >fake.sendmail
     chmod +x ./fake.sendmail
     git add fake.sendmail
     GIT_AUTHOR_NAME="A" git commit -a -m "Second."'

test_expect_success 'Extract patches' '
    patches=`git format-patch -n HEAD^1`
'

test_expect_success 'Send patches' '
     git send-email --from="Example <nobody@example.com>" --to=nobody@example.com --smtp-server="$(pwd)/fake.sendmail" $patches 2>errors
'

cat >expected <<\EOF
!nobody@example.com!
!author@example.com!
EOF
test_expect_success \
    'Verify commandline' \
    'diff commandline expected'

test_done
