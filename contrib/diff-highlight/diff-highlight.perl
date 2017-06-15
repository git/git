package main;

# Some scripts may not realize that SIGPIPE is being ignored when launching the
# pager--for instance scripts written in Python.
$SIG{PIPE} = 'DEFAULT';

DiffHighlight::highlight_stdin();
exit 0;
