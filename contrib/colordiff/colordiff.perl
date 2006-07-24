#!/usr/bin/perl -w
#
# $Id: colordiff.pl,v 1.4.2.10 2004/01/04 15:02:59 daveewart Exp $

########################################################################
#                                                                      #
# ColorDiff - a wrapper/replacment for 'diff' producing                #
#             colourful output                                         #
#                                                                      #
# Copyright (C)2002-2004 Dave Ewart (davee@sungate.co.uk)              #
#                                                                      #
########################################################################
#                                                                      #
# This program is free software; you can redistribute it and/or modify #
# it under the terms of the GNU General Public License as published by #
# the Free Software Foundation; either version 2 of the License, or    #
# (at your option) any later version.                                  #
#                                                                      #
# This program is distributed in the hope that it will be useful,      #
# but WITHOUT ANY WARRANTY; without even the implied warranty of       #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        #
# GNU General Public License for more details.                         #
#                                                                      #
# You should have received a copy of the GNU General Public License    #
# along with this program; if not, write to the Free Software          #
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.            #
#                                                                      #
########################################################################

use strict;
use Getopt::Long qw(:config pass_through);
use IPC::Open2;

my $app_name     = 'colordiff';
my $version      = '1.0.4';
my $author       = 'Dave Ewart';
my $author_email = 'davee@sungate.co.uk';
my $app_www      = 'http://colordiff.sourceforge.net/';
my $copyright    = '(C)2002-2004';
my $show_banner  = 1;

# ANSI sequences for colours
my %colour;
$colour{white}       = "\033[1;37m";
$colour{yellow}      = "\033[1;33m";
$colour{green}       = "\033[1;32m";
$colour{blue}        = "\033[1;34m";
$colour{cyan}        = "\033[1;36m";
$colour{red}         = "\033[1;31m";
$colour{magenta}     = "\033[1;35m";
$colour{black}       = "\033[1;30m";
$colour{darkwhite}   = "\033[0;37m";
$colour{darkyellow}  = "\033[0;33m";
$colour{darkgreen}   = "\033[0;32m";
$colour{darkblue}    = "\033[0;34m";
$colour{darkcyan}    = "\033[0;36m";
$colour{darkred}     = "\033[0;31m";
$colour{darkmagenta} = "\033[0;35m";
$colour{darkblack}   = "\033[0;30m";
$colour{OFF}         = "\033[0;0m";

# Default colours if /etc/colordiffrc or ~/.colordiffrc do not exist
my $plain_text = $colour{OFF};
my $file_old   = $colour{red};
my $file_new   = $colour{blue};
my $diff_stuff = $colour{magenta};

# Locations for personal and system-wide colour configurations
my $HOME   = $ENV{HOME};
my $etcdir = '/etc';

my ($setting, $value);
my @config_files = ("$etcdir/colordiffrc", "$HOME/.colordiffrc");
my $config_file;

foreach $config_file (@config_files) {
    if (open(COLORDIFFRC, "<$config_file")) {
        while (<COLORDIFFRC>) {
            chop;
            next if (/^#/ || /^$/);
            s/\s+//g;
            ($setting, $value) = split ('=');
            if ($setting eq 'banner') {
                if ($value eq 'no') {
                    $show_banner = 0;
                }
                next;
            }
            if (!defined $colour{$value}) {
                print "Invalid colour specification ($value) in $config_file\n";
                next;
            }
            if ($setting eq 'plain') {
                $plain_text = $colour{$value};
            }
            elsif ($setting eq 'oldtext') {
                $file_old = $colour{$value};
            }
            elsif ($setting eq 'newtext') {
                $file_new = $colour{$value};
            }
            elsif ($setting eq 'diffstuff') {
                $diff_stuff = $colour{$value};
            }
            else {
                print "Unknown option in $etcdir/colordiffrc: $setting\n";
            }
        }
        close COLORDIFFRC;
    }
}

# colordiff specific options here.  Need to pre-declare if using variables
GetOptions(
    "no-banner" => sub { $show_banner = 0 },
    "plain-text=s" => \&set_color,
    "file-old=s"   => \&set_color,
    "file-new=s"   => \&set_color,
    "diff-stuff=s" => \&set_color
);

if ($show_banner == 1) {
    print STDERR "$app_name $version ($app_www)\n";
    print STDERR "$copyright $author, $author_email\n\n";
}

if (defined $ARGV[0]) {
    # More reliable way of pulling in arguments
    open2(\*INPUTSTREAM, undef, "git", "diff", @ARGV);
}
else {
    *INPUTSTREAM = \*STDIN;
}

my $record;
my $nrecs           = 0;
my $inside_file_old = 1;
my $nparents        = undef;

while (<INPUTSTREAM>) {
    $nrecs++;
    if (/^(\@\@+) -[-+0-9, ]+ \1/) {
	    print "$diff_stuff";
	    $nparents = length($1) - 1;
    }
    elsif (/^diff -/ || /^index / ||
	   /^old mode / || /^new mode / ||
	   /^deleted file mode / || /^new file mode / ||
	   /^similarity index / || /^dissimilarity index / ||
	   /^copy from / || /^copy to / ||
	   /^rename from / || /^rename to /) {
	    $nparents = undef;
	    print "$diff_stuff";
    }
    elsif (defined $nparents) {
	    if ($nparents == 1) {
		    if (/^\+/) {
			    print $file_new;
		    }
		    elsif (/^-/) {
			    print $file_old;
		    }
		    else {
			    print $plain_text;
		    }
	    }
	    elsif (/^ {$nparents}/) {
		    print "$plain_text";
	    }
	    elsif (/^[+ ]{$nparents}/) {
		    print "$file_new";
	    }
	    elsif (/^[- ]{$nparents}/) {
		    print "$file_old";
	    }
	    else {
		    print $plain_text;
	    }
    }
    elsif (/^--- / || /^\+\+\+ /) {
	    print $diff_stuff;
    }
    else {
	    print "$plain_text";
    }
    s/$/$colour{OFF}/;
    print "$_";
}
close INPUTSTREAM;

sub set_color {
    my ($type, $color) = @_;

    $type =~ s/-/_/;
    eval "\$$type = \$colour{$color}";
}
