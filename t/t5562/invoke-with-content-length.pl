require v5.26;
use strict;
use warnings;

my $body_filename = $ARGV[0];
my @command = @ARGV[1 .. $#ARGV];

# read data
my $body_size = -s $body_filename;
$ENV{"CONTENT_LENGTH"} = $body_size;
open(my $body_fh, "<", $body_filename) or die "Cannot open $body_filename: $!";
my $body_data;
defined read($body_fh, $body_data, $body_size) or die "Cannot read $body_filename: $!";
close($body_fh);

# write data
my $pid = open(my $out, "|-", @command);
{
        # disable buffering at $out
        my $old_selected = select;
        select $out;
        $| = 1;
        select $old_selected;
}
print $out $body_data or die "Cannot write data: $!";

$SIG{ALRM} = sub {
        kill 'KILL', $pid;
        die "Command did not exit after reading whole body";
};
alarm 60;

my $ret = waitpid($pid, 0);
if ($ret != $pid) {
        die "confusing return from waitpid: $ret";
}
