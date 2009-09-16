package Generators::QMake;
require Exporter;

use strict;
use vars qw($VERSION);

our $VERSION = '1.00';
our(@ISA, @EXPORT, @EXPORT_OK, @AVAILABLE);
@ISA = qw(Exporter);

BEGIN {
    push @EXPORT_OK, qw(generate);
}

sub generate {
    my ($git_dir, $out_dir, $rel_dir, %build_structure) = @_;

    my @libs = @{$build_structure{"LIBS"}};
    foreach (@libs) {
        createLibProject($_, $git_dir, $out_dir, $rel_dir, %build_structure);
    }

    my @apps = @{$build_structure{"APPS"}};
    foreach (@apps) {
        createAppProject($_, $git_dir, $out_dir, $rel_dir, %build_structure);
    }

    createGlueProject($git_dir, $out_dir, $rel_dir, %build_structure);
    return 0;
}

sub createLibProject {
    my ($libname, $git_dir, $out_dir, $rel_dir, %build_structure) = @_;
    print "Generate $libname lib project\n";
    $rel_dir = "../$rel_dir";

    my $sources = join(" \\\n\t", sort(map("$rel_dir/$_", @{$build_structure{"LIBS_${libname}_SOURCES"}})));
    my $defines = join(" \\\n\t", sort(@{$build_structure{"LIBS_${libname}_DEFINES"}}));
    my $includes= join(" \\\n\t", sort(map("$rel_dir/$_", @{$build_structure{"LIBS_${libname}_INCLUDES"}})));
    my $cflags  = join(" ", sort(@{$build_structure{"LIBS_${libname}_CFLAGS"}}));

    my $cflags_debug = $cflags;
    $cflags_debug =~ s/-MT/-MTd/;
    $cflags_debug =~ s/-O.//;

    my $cflags_release = $cflags;
    $cflags_release =~ s/-MTd/-MT/;

    my @tmp  = @{$build_structure{"LIBS_${libname}_LFLAGS"}};
    my @tmp2 = ();
    foreach (@tmp) {
        if (/^-LTCG/) {
        } elsif (/^-L/) {
            $_ =~ s/^-L/-LIBPATH:$rel_dir\//;
        }
        push(@tmp2, $_);
    }
    my $lflags = join(" ", sort(@tmp));

    my $target = $libname;
    $target =~ s/\//_/g;
    $defines =~ s/-D//g;
    $defines =~ s/"/\\\\"/g;
    $includes =~ s/-I//g;
    mkdir "$target" || die "Could not create the directory $target for lib project!\n";
    open F, ">$target/$target.pro" || die "Could not open $target/$target.pro for writing!\n";
    print F << "EOM";
TEMPLATE = lib
TARGET = $target
DESTDIR = $rel_dir

CONFIG -= qt
CONFIG += static

QMAKE_CFLAGS =
QMAKE_CFLAGS_RELEASE = $cflags_release
QMAKE_CFLAGS_DEBUG = $cflags_debug
QMAKE_LIBFLAGS = $lflags

DEFINES += \\
        $defines

INCLUDEPATH += \\
        $includes

SOURCES += \\
        $sources
EOM
    close F;
}

sub createAppProject {
    my ($appname, $git_dir, $out_dir, $rel_dir, %build_structure) = @_;
    print "Generate $appname app project\n";
    $rel_dir = "../$rel_dir";

    my $sources = join(" \\\n\t", sort(map("$rel_dir/$_", @{$build_structure{"APPS_${appname}_SOURCES"}})));
    my $defines = join(" \\\n\t", sort(@{$build_structure{"APPS_${appname}_DEFINES"}}));
    my $includes= join(" \\\n\t", sort(map("$rel_dir/$_", @{$build_structure{"APPS_${appname}_INCLUDES"}})));
    my $cflags  = join(" ", sort(@{$build_structure{"APPS_${appname}_CFLAGS"}}));

    my $cflags_debug = $cflags;
    $cflags_debug =~ s/-MT/-MTd/;
    $cflags_debug =~ s/-O.//;

    my $cflags_release = $cflags;
    $cflags_release =~ s/-MTd/-MT/;

    my $libs;
    foreach (sort(@{$build_structure{"APPS_${appname}_LIBS"}})) {
        $_ =~ s/\//_/g;
        $libs .= " $_";
    }
    my @tmp  = @{$build_structure{"APPS_${appname}_LFLAGS"}};
    my @tmp2 = ();
    foreach (@tmp) {
        # next if ($_ eq "-NODEFAULTLIB:MSVCRT.lib");
        if (/^-LTCG/) {
        } elsif (/^-L/) {
            $_ =~ s/^-L/-LIBPATH:$rel_dir\//;
        }
        push(@tmp2, $_);
    }
    my $lflags = join(" ", sort(@tmp));

    my $target = $appname;
    $target =~ s/\.exe//;
    $target =~ s/\//_/g;
    $defines =~ s/-D//g;
    $defines =~ s/"/\\\\"/g;
    $includes =~ s/-I//g;
    mkdir "$target" || die "Could not create the directory $target for app project!\n";
    open F, ">$target/$target.pro" || die "Could not open $target/$target.pro for writing!\n";
    print F << "EOM";
TEMPLATE = app
TARGET = $target
DESTDIR = $rel_dir

CONFIG -= qt embed_manifest_exe
CONFIG += console

QMAKE_CFLAGS =
QMAKE_CFLAGS_RELEASE = $cflags_release
QMAKE_CFLAGS_DEBUG = $cflags_debug
QMAKE_LFLAGS = $lflags
LIBS   = $libs

DEFINES += \\
        $defines

INCLUDEPATH += \\
        $includes

win32:QMAKE_LFLAGS += -LIBPATH:$rel_dir
else: QMAKE_LFLAGS += -L$rel_dir

SOURCES += \\
        $sources
EOM
    close F;
}

sub createGlueProject {
    my ($git_dir, $out_dir, $rel_dir, %build_structure) = @_;
    my $libs = join(" \\ \n", map("\t$_|$_.pro", @{$build_structure{"LIBS"}}));
    my $apps = join(" \\ \n", map("\t$_|$_.pro", @{$build_structure{"APPS"}}));
    $libs =~ s/\.a//g;
    $libs =~ s/\//_/g;
    $libs =~ s/\|/\//g;
    $apps =~ s/\.exe//g;
    $apps =~ s/\//_/g;
    $apps =~ s/\|/\//g;

    my $filename = $out_dir;
    $filename =~ s/.*\/([^\/]+)$/$1/;
    $filename =~ s/\/$//;
    print "Generate glue project $filename.pro\n";
    open F, ">$filename.pro" || die "Could not open $filename.pro for writing!\n";
    print F << "EOM";
TEMPLATE = subdirs
CONFIG += ordered
SUBDIRS += \\
$libs \\
$apps
EOM
    close F;
}

1;
