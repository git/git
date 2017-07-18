#!/usr/bin/perl -w
######################################################################
# Do not call this script directly!
#
# The generate script ensures that @INC is correct before the engine
# is executed.
#
# Copyright (C) 2009 Marius Storm-Olsen <mstormo@gmail.com>
######################################################################
use strict;
use File::Basename;
use Cwd;

my $file = $ARGV[0];
die "No file provided!" if !defined $file;

my ($cflags, $target, $type, $line);

open(F, "<$file") || die "Couldn't open file $file";
my @data = <F>;
close(F);

while (my $text = shift @data) {
    my $ate_next;
    do {
        $ate_next = 0;
        $line++;
        chomp $text;
        chop $text if ($text =~ /\r$/);
        if ($text =~ /\\$/) {
            $text =~ s/\\$//;
            $text .= shift @data;
            $ate_next = 1;
        }
    } while($ate_next);

    if($text =~ / -c /) {
        # compilation
        handleCompileLine($text, $line);

    } elsif ($text =~ / -o /) {
        # linking executable
        handleLinkLine($text, $line);

    } elsif ($text =~ /\.o / && $text =~ /\.a /) {
        # libifying
        handleLibLine($text, $line);

#    } elsif ($text =~ /^cp /) {
#        # copy file around
#
#    } elsif ($text =~ /^rm -f /) {
#        # shell command
#
#    } elsif ($text =~ /^make[ \[]/) {
#        # make output
#
#    } elsif ($text =~ /^echo /) {
#        # echo to file
#
#    } elsif ($text =~ /^if /) {
#        # shell conditional
#
#    } elsif ($text =~ /^tclsh /) {
#        # translation stuff
#
#    } elsif ($text =~ /^umask /) {
#        # handling boilerplates
#
#    } elsif ($text =~ /\$\(\:\)/) {
#        # ignore
#
#    } elsif ($text =~ /^FLAGS=/) {
#        # flags check for dependencies
#
#    } elsif ($text =~ /^'\/usr\/bin\/perl' -MError -e/) {
#        # perl commands for copying files
#
#    } elsif ($text =~ /generate-cmdlist\.sh/) {
#        # command for generating list of commands
#
#    } elsif ($text =~ /^test / && $text =~ /|| rm -f /) {
#        # commands removing executables, if they exist
#
#    } elsif ($text =~ /new locations or Tcl/) {
#        # command for detecting Tcl/Tk changes
#
#    } elsif ($text =~ /mkdir -p/) {
#        # command creating path
#
#    } elsif ($text =~ /: no custom templates yet/) {
#        # whatever

    } else {
#        print "Unhandled (line: $line): $text\n";
    }
}
close(F);

# use Data::Dumper;
# print "Parsed build structure:\n";
# print Dumper(%build_structure);

# -------------------------------------------------------------------
# Functions under here
# -------------------------------------------------------------------
my (%build_structure, @defines, @incpaths, @cflags, @sources);

sub clearCompileStep
{
    @defines = ();
    @incpaths = ();
    @cflags = ();
    @sources = ();
}

sub removeDuplicates
{
    my (%dupHash, $entry);
    %dupHash = map { $_, 1 } @defines;
    @defines = keys %dupHash;

    %dupHash = map { $_, 1 } @incpaths;
    @incpaths = keys %dupHash;

    %dupHash = map { $_, 1 } @cflags;
    @cflags = keys %dupHash;

    %dupHash = map { $_, 1 } @sources;
    @sources = keys %dupHash;
}

sub handleCompileLine
{
    my ($line, $lineno) = @_;
    my @parts = split(' ', $line);
    shift(@parts); # ignore cmd
    while (my $part = shift @parts) {
        if ("$part" eq "-o") {
            # ignore object file
            shift @parts;
        } elsif ("$part" eq "-c") {
            # ignore compile flag
        } elsif ("$part" eq "-c") {
        } elsif ($part =~ /^.?-I/) {
            push(@incpaths, $part);
        } elsif ($part =~ /^.?-D/) {
            push(@defines, $part);
        } elsif ($part =~ /^-/) {
            push(@cflags, $part);
        } elsif ($part =~ /\.(c|cc|cpp)$/) {
            push(@sources, $part);
        } else {
            die "Unhandled compiler option @ line $lineno: $part";
        }
    }
    #print "Sources: @sources\nCFlags: @cflags\nDefine: @defines\nIncpat: @incpaths\n";
    #exit(1);
}

sub handleLibLine
{
    my ($line, $lineno) = @_;
    my (@objfiles, @lflags, $libout, $part);
    # kill cmd and rm 'prefix'
    $line =~ s/^rm -f .* && .* rcs //;
    my @parts = split(' ', $line);
    while ($part = shift @parts) {
        if ($part =~ /^-/) {
            push(@lflags, $part);
        } elsif ($part =~ /\.(o|obj)$/) {
            push(@objfiles, $part);
        } elsif ($part =~ /\.(a|lib)$/) {
            $libout = $part;
        } else {
            die "Unhandled lib option @ line $lineno: $part";
        }
    }
    #print "LibOut: '$libout'\nLFlags: @lflags\nOfiles: @objfiles\n";
    #exit(1);
    removeDuplicates();
    push(@{$build_structure{"LIBS"}}, $libout);
    @{$build_structure{"LIBS_${libout}"}} = ("_DEFINES", "_INCLUDES", "_CFLAGS", "_SOURCES",
                                             "_OBJECTS");
    @{$build_structure{"LIBS_${libout}_DEFINES"}} = @defines;
    @{$build_structure{"LIBS_${libout}_INCLUDES"}} = @incpaths;
    @{$build_structure{"LIBS_${libout}_CFLAGS"}} = @cflags;
    @{$build_structure{"LIBS_${libout}_SOURCES"}} = @sources;
    @{$build_structure{"LIBS_${libout}_OBJECTS"}} = @objfiles;
    clearCompileStep();
}

sub handleLinkLine
{
    my ($line, $lineno) = @_;
    my (@objfiles, @lflags, @libs, $appout, $part);
    my @parts = split(' ', $line);
    shift(@parts); # ignore cmd
    while ($part = shift @parts) {
        if ($part =~ /^-[GRIDO]/) {
            # eat compiler flags
        } elsif ("$part" eq "-o") {
            $appout = shift @parts;
        } elsif ($part =~ /^-/) {
            push(@lflags, $part);
        } elsif ($part =~ /\.(a|lib)$/) {
            push(@libs, $part);
        } elsif ($part =~ /\.(o|obj)$/) {
            push(@objfiles, $part);
        } else {
            die "Unhandled lib option @ line $lineno: $part";
        }
    }
    #print "AppOut: '$appout'\nLFlags: @lflags\nLibs  : @libs\nOfiles: @objfiles\n";
    #exit(1);
    removeDuplicates();
    push(@{$build_structure{"APPS"}}, $appout);
    @{$build_structure{"APPS_${appout}"}} = ("_DEFINES", "_INCLUDES", "_CFLAGS", "_LFLAGS",
                                             "_SOURCES", "_OBJECTS", "_LIBS");
    @{$build_structure{"APPS_${appout}_DEFINES"}} = @defines;
    @{$build_structure{"APPS_${appout}_INCLUDES"}} = @incpaths;
    @{$build_structure{"APPS_${appout}_CFLAGS"}} = @cflags;
    @{$build_structure{"APPS_${appout}_LFLAGS"}} = @lflags;
    @{$build_structure{"APPS_${appout}_SOURCES"}} = @sources;
    @{$build_structure{"APPS_${appout}_OBJECTS"}} = @objfiles;
    @{$build_structure{"APPS_${appout}_LIBS"}} = @libs;
    clearCompileStep();
}
