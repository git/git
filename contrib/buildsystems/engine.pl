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
use File::Spec;
use Cwd;
use Generators;

my (%build_structure, %compile_options, @makedry);
my $out_dir = getcwd();
my $git_dir = $out_dir;
$git_dir =~ s=\\=/=g;
$git_dir = dirname($git_dir) while (!-e "$git_dir/git.c" && "$git_dir" ne "");
die "Couldn't find Git repo" if ("$git_dir" eq "");

my @gens = Generators::available();
my $gen = "Vcproj";

sub showUsage
{
    my $genlist = join(', ', @gens);
    print << "EOM";
generate usage:
  -g <GENERATOR>  --gen <GENERATOR> Specify the buildsystem generator    (default: $gen)
                                    Available: $genlist
  -o <PATH>       --out <PATH>      Specify output directory generation  (default: .)
  -i <FILE>       --in <FILE>       Specify input file, instead of running GNU Make
  -h,-?           --help            This help
EOM
    exit 0;
}

# Parse command-line options
while (@ARGV) {
    my $arg = shift @ARGV;
    if ("$arg" eq "-h" || "$arg" eq "--help" || "$arg" eq "-?") {
	showUsage();
	exit(0);
    } elsif("$arg" eq "--out" || "$arg" eq "-o") {
	$out_dir = shift @ARGV;
    } elsif("$arg" eq "--gen" || "$arg" eq "-g") {
	$gen = shift @ARGV;
    } elsif("$arg" eq "--in" || "$arg" eq "-i") {
	my $infile = shift @ARGV;
        open(F, "<$infile") || die "Couldn't open file $infile";
        @makedry = <F>;
        close(F);
    }
}

# NOT using File::Spec->rel2abs($path, $base) here, as
# it fails badly for me in the msysgit environment
$git_dir = File::Spec->rel2abs($git_dir);
$out_dir = File::Spec->rel2abs($out_dir);
my $rel_dir = makeOutRel2Git($git_dir, $out_dir);

# Print some information so the user feels informed
print << "EOM";
-----
Generator: $gen
Git dir:   $git_dir
Out dir:   $out_dir
-----
Running GNU Make to figure out build structure...
EOM

# Pipe a make --dry-run into a variable, if not already loaded from file
@makedry = `cd $git_dir && make -n MSVC=1 V=1 2>/dev/null` if !@makedry;

# Parse the make output into usable info
parseMakeOutput();

# Finally, ask the generator to start generating..
Generators::generate($gen, $git_dir, $out_dir, $rel_dir, %build_structure);

# main flow ends here
# -------------------------------------------------------------------------------------------------


# 1) path: /foo/bar/baz        2) path: /foo/bar/baz   3) path: /foo/bar/baz
#    base: /foo/bar/baz/temp      base: /foo/bar          base: /tmp
#    rel:  ..                     rel:  baz               rel:  ../foo/bar/baz
sub makeOutRel2Git
{
    my ($path, $base) = @_;
    my $rel;
    if ("$path" eq "$base") {
        return ".";
    } elsif ($base =~ /^$path/) {
        # case 1
        my $tmp = $base;
        $tmp =~ s/^$path//;
        foreach (split('/', $tmp)) {
            $rel .= "../" if ("$_" ne "");
        }
    } elsif ($path =~ /^$base/) {
        # case 2
        $rel = $path;
        $rel =~ s/^$base//;
        $rel = "./$rel";
    } else {
        my $tmp = $base;
        foreach (split('/', $tmp)) {
            $rel .= "../" if ("$_" ne "");
        }
        $rel .= $path;
    }
    $rel =~ s/\/\//\//g; # simplify
    $rel =~ s/\/$//;     # don't end with /
    return $rel;
}

sub parseMakeOutput
{
    print "Parsing GNU Make output to figure out build structure...\n";
    my $line = 0;
    while (my $text = shift @makedry) {
        my $ate_next;
        do {
            $ate_next = 0;
            $line++;
            chomp $text;
            chop $text if ($text =~ /\r$/);
            if ($text =~ /\\$/) {
                $text =~ s/\\$//;
                $text .= shift @makedry;
                $ate_next = 1;
            }
        } while($ate_next);

        if ($text =~ /^test /) {
            # options to test (eg -o) may be mistaken for linker options
            next;
        }

        if($text =~ / -c /) {
            # compilation
            handleCompileLine($text, $line);

        } elsif ($text =~ / -o /) {
            # linking executable
            handleLinkLine($text, $line);

        } elsif ($text =~ /\.o / && $text =~ /\.a /) {
            # libifying
            handleLibLine($text, $line);
#
#        } elsif ($text =~ /^cp /) {
#            # copy file around
#
#        } elsif ($text =~ /^rm -f /) {
#            # shell command
#
#        } elsif ($text =~ /^make[ \[]/) {
#            # make output
#
#        } elsif ($text =~ /^echo /) {
#            # echo to file
#
#        } elsif ($text =~ /^if /) {
#            # shell conditional
#
#        } elsif ($text =~ /^tclsh /) {
#            # translation stuff
#
#        } elsif ($text =~ /^umask /) {
#            # handling boilerplates
#
#        } elsif ($text =~ /\$\(\:\)/) {
#            # ignore
#
#        } elsif ($text =~ /^FLAGS=/) {
#            # flags check for dependencies
#
#        } elsif ($text =~ /^'\/usr\/bin\/perl' -MError -e/) {
#            # perl commands for copying files
#
#        } elsif ($text =~ /generate-cmdlist\.sh/) {
#            # command for generating list of commands
#
#        } elsif ($text =~ /new locations or Tcl/) {
#            # command for detecting Tcl/Tk changes
#
#        } elsif ($text =~ /mkdir -p/) {
#            # command creating path
#
#        } elsif ($text =~ /: no custom templates yet/) {
#            # whatever
#
#        } else {
#            print "Unhandled (line: $line): $text\n";
        }
    }

#    use Data::Dumper;
#    print "Parsed build structure:\n";
#    print Dumper(%build_structure);
}

# variables for the compilation part of each step
my (@defines, @incpaths, @cflags, @sources);

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
}

sub handleCompileLine
{
    my ($line, $lineno) = @_;
    my @parts = split(' ', $line);
    my $sourcefile;
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
            $sourcefile = $part;
        } else {
            die "Unhandled compiler option @ line $lineno: $part";
        }
    }
    @{$compile_options{"${sourcefile}_CFLAGS"}} = @cflags;
    @{$compile_options{"${sourcefile}_DEFINES"}} = @defines;
    @{$compile_options{"${sourcefile}_INCPATHS"}} = @incpaths;
    clearCompileStep();
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
            $libout =~ s/\.a$//;
        } else {
            die "Unhandled lib option @ line $lineno: $part";
        }
    }
#    print "LibOut: '$libout'\nLFlags: @lflags\nOfiles: @objfiles\n";
#    exit(1);
    foreach (@objfiles) {
        my $sourcefile = $_;
        $sourcefile =~ s/\.o$/.c/;
        push(@sources, $sourcefile);
        push(@cflags, @{$compile_options{"${sourcefile}_CFLAGS"}});
        push(@defines, @{$compile_options{"${sourcefile}_DEFINES"}});
        push(@incpaths, @{$compile_options{"${sourcefile}_INCPATHS"}});
    }
    removeDuplicates();

    push(@{$build_structure{"LIBS"}}, $libout);
    @{$build_structure{"LIBS_${libout}"}} = ("_DEFINES", "_INCLUDES", "_CFLAGS", "_SOURCES",
                                             "_OBJECTS");
    @{$build_structure{"LIBS_${libout}_DEFINES"}} = @defines;
    @{$build_structure{"LIBS_${libout}_INCLUDES"}} = @incpaths;
    @{$build_structure{"LIBS_${libout}_CFLAGS"}} = @cflags;
    @{$build_structure{"LIBS_${libout}_LFLAGS"}} = @lflags;
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
        if ($part =~ /^-IGNORE/) {
            push(@lflags, $part);
        } elsif ($part =~ /^-[GRIMDO]/) {
            # eat compiler flags
        } elsif ("$part" eq "-o") {
            $appout = shift @parts;
        } elsif ("$part" eq "-lz") {
            push(@libs, "zlib.lib");
	} elsif ("$part" eq "-lcrypto") {
            push(@libs, "libeay32.lib");
        } elsif ("$part" eq "-lssl") {
            push(@libs, "ssleay32.lib");
        } elsif ($part =~ /^-/) {
            push(@lflags, $part);
        } elsif ($part =~ /\.(a|lib)$/) {
            $part =~ s/\.a$/.lib/;
            push(@libs, $part);
        } elsif ($part eq 'invalidcontinue.obj') {
            # ignore - known to MSVC
        } elsif ($part =~ /\.o$/) {
            push(@objfiles, $part);
        } elsif ($part =~ /\.obj$/) {
            # do nothing, 'make' should not be producing .obj, only .o files
        } else {
            die "Unhandled lib option @ line $lineno: $part";
        }
    }
#    print "AppOut: '$appout'\nLFlags: @lflags\nLibs  : @libs\nOfiles: @objfiles\n";
#    exit(1);
    foreach (@objfiles) {
        my $sourcefile = $_;
        $sourcefile =~ s/\.o$/.c/;
        push(@sources, $sourcefile);
        push(@cflags, @{$compile_options{"${sourcefile}_CFLAGS"}});
        push(@defines, @{$compile_options{"${sourcefile}_DEFINES"}});
        push(@incpaths, @{$compile_options{"${sourcefile}_INCPATHS"}});
    }
    removeDuplicates();

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
