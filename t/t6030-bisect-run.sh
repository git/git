#!/bin/sh
#
# Copyright (c) 2007 Christian Couder
#
test_description='Tests git-bisect run functionality'

. ./test-lib.sh

add_line_into_file()
{
    _line=$1
    _file=$2

    if [ -f "$_file" ]; then
        echo "$_line" >> $_file || return $?
        MSG="Add <$_line> into <$_file>."
    else
        echo "$_line" > $_file || return $?
        git add $_file || return $?
        MSG="Create file <$_file> with <$_line> inside."
    fi

    git-commit -m "$MSG" $_file
}

HASH1=
HASH3=
HASH4=

test_expect_success \
    'set up basic repo with 1 file (hello) and 4 commits' \
    'add_line_into_file "1: Hello World" hello &&
     add_line_into_file "2: A new day for git" hello &&
     add_line_into_file "3: Another new day for git" hello &&
     add_line_into_file "4: Ciao for now" hello &&
     HASH1=$(git rev-list HEAD | tail -1) &&
     HASH3=$(git rev-list HEAD | head -2 | tail -1) &&
     HASH4=$(git rev-list HEAD | head -1)'

# We want to automatically find the commit that
# introduced "Another" into hello.
test_expect_success \
    'git bisect run simple case' \
    'echo "#!/bin/sh" > test_script.sh &&
     echo "grep Another hello > /dev/null" >> test_script.sh &&
     echo "test \$? -ne 0" >> test_script.sh &&
     chmod +x test_script.sh &&
     git bisect start &&
     git bisect good $HASH1 &&
     git bisect bad $HASH4 &&
     git bisect run ./test_script.sh > my_bisect_log.txt &&
     grep "$HASH3 is first bad commit" my_bisect_log.txt'

#
#
test_done

