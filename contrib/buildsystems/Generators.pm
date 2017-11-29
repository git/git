package Generators;
require Exporter;

use strict;
use File::Basename;
no strict 'refs';
use vars qw($VERSION @AVAILABLE);

our $VERSION = '1.00';
our(@ISA, @EXPORT, @EXPORT_OK, @AVAILABLE);
@ISA = qw(Exporter);

BEGIN {
    local(*D);
    my $me = $INC{"Generators.pm"};
    die "Couldn't find myself in \@INC, which is required to load the generators!" if ("$me" eq "");
    $me = dirname($me);
    if (opendir(D,"$me/Generators")) {
        foreach my $gen (readdir(D)) {
            next unless ($gen  =~ /\.pm$/);
            require "${me}/Generators/$gen";
            $gen =~ s,\.pm,,;
            push(@AVAILABLE, $gen);
        }
        closedir(D);
        my $gens = join(', ', @AVAILABLE);
    }

    push @EXPORT_OK, qw(available);
}

sub available {
    return @AVAILABLE;
}

sub generate {
    my ($gen, $git_dir, $out_dir, $rel_dir, %build_structure) = @_;
    return eval("Generators::${gen}::generate(\$git_dir, \$out_dir, \$rel_dir, \%build_structure)") if grep(/^$gen$/, @AVAILABLE);
    die "Generator \"${gen}\" is not available!\nAvailable generators are: @AVAILABLE\n";
}

1;
