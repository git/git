package Generators::Vcproj;
require Exporter;

use strict;
use vars qw($VERSION);
use Digest::SHA qw(sha256_hex);

our $VERSION = '1.00';
our(@ISA, @EXPORT, @EXPORT_OK, @AVAILABLE);
@ISA = qw(Exporter);

BEGIN {
    push @EXPORT_OK, qw(generate);
}

sub generate_guid ($) {
    my $hex = sha256_hex($_[0]);
    $hex =~ s/^(.{8})(.{4})(.{4})(.{4})(.{12}).*/{$1-$2-$3-$4-$5}/;
    $hex =~ tr/a-z/A-Z/;
    return $hex;
}

sub generate {
    my ($git_dir, $out_dir, $rel_dir, %build_structure) = @_;
    my @libs = @{$build_structure{"LIBS"}};
    foreach (@libs) {
        createLibProject($_, $git_dir, $out_dir, $rel_dir, \%build_structure);
    }

    my @apps = @{$build_structure{"APPS"}};
    foreach (@apps) {
        createAppProject($_, $git_dir, $out_dir, $rel_dir, \%build_structure);
    }

    createGlueProject($git_dir, $out_dir, $rel_dir, %build_structure);
    return 0;
}

sub createLibProject {
    my ($libname, $git_dir, $out_dir, $rel_dir, $build_structure) = @_;
    print "Generate $libname vcproj lib project\n";
    $rel_dir = "..\\$rel_dir";
    $rel_dir =~ s/\//\\/g;

    my $target = $libname;
    $target =~ s/\//_/g;
    $target =~ s/\.a//;

    my $uuid = generate_guid($libname);
    $$build_structure{"LIBS_${target}_GUID"} = $uuid;

    my @srcs = sort(map("$rel_dir\\$_", @{$$build_structure{"LIBS_${libname}_SOURCES"}}));
    my @sources;
    foreach (@srcs) {
        $_ =~ s/\//\\/g;
        push(@sources, $_);
    }
    my $defines = join(",", sort(@{$$build_structure{"LIBS_${libname}_DEFINES"}}));
    my $includes= join(";", sort(map("&quot;$rel_dir\\$_&quot;", @{$$build_structure{"LIBS_${libname}_INCLUDES"}})));
    my $cflags  = join(" ", sort(@{$$build_structure{"LIBS_${libname}_CFLAGS"}}));
    $cflags =~ s/\"/&quot;/g;
    $cflags =~ s/</&lt;/g;
    $cflags =~ s/>/&gt;/g;

    my $cflags_debug = $cflags;
    $cflags_debug =~ s/-MT/-MTd/;
    $cflags_debug =~ s/-O.//;

    my $cflags_release = $cflags;
    $cflags_release =~ s/-MTd/-MT/;

    my @tmp  = @{$$build_structure{"LIBS_${libname}_LFLAGS"}};
    my @tmp2 = ();
    foreach (@tmp) {
        if (/^-LTCG/) {
        } elsif (/^-L/) {
            $_ =~ s/^-L/-LIBPATH:$rel_dir\//;
        }
        push(@tmp2, $_);
    }
    my $lflags = join(" ", sort(@tmp));

    $defines =~ s/-D//g;
    $defines =~ s/\"/\\&quot;/g;
    $defines =~ s/</&lt;/g;
    $defines =~ s/>/&gt;/g;
    $defines =~ s/\'//g;
    $includes =~ s/-I//g;
    mkdir "$target" || die "Could not create the directory $target for lib project!\n";
    open F, ">$target/$target.vcproj" || die "Could not open $target/$target.pro for writing!\n";
    binmode F, ":crlf";
    print F << "EOM";
<?xml version="1.0" encoding = "Windows-1252"?>
<VisualStudioProject
	ProjectType="Visual C++"
	Version="9,00"
	Name="$target"
	ProjectGUID="$uuid">
	<Platforms>
		<Platform
			Name="Win32"/>
	</Platforms>
	<ToolFiles>
	</ToolFiles>
	<Configurations>
		<Configuration
			Name="Debug|Win32"
			OutputDirectory="$rel_dir"
			ConfigurationType="4"
			CharacterSet="0"
			IntermediateDirectory="\$(ProjectDir)\$(ConfigurationName)"
			>
			<Tool
				Name="VCPreBuildEventTool"
			/>
			<Tool
				Name="VCCustomBuildTool"
			/>
			<Tool
				Name="VCXMLDataGeneratorTool"
			/>
			<Tool
				Name="VCMIDLTool"
			/>
			<Tool
				Name="VCCLCompilerTool"
				AdditionalOptions="$cflags_debug"
				Optimization="0"
				InlineFunctionExpansion="1"
				AdditionalIncludeDirectories="$includes"
				PreprocessorDefinitions="WIN32,_DEBUG,$defines"
				MinimalRebuild="true"
				RuntimeLibrary="1"
				UsePrecompiledHeader="0"
				ProgramDataBaseFileName="\$(IntDir)\\\$(TargetName).pdb"
				WarningLevel="3"
				DebugInformationFormat="3"
			/>
			<Tool
				Name="VCManagedResourceCompilerTool"
			/>
			<Tool
				Name="VCResourceCompilerTool"
			/>
			<Tool
				Name="VCPreLinkEventTool"
			/>
			<Tool
				Name="VCLibrarianTool"
				SuppressStartupBanner="true"
			/>
			<Tool
				Name="VCALinkTool"
			/>
			<Tool
				Name="VCXDCMakeTool"
			/>
			<Tool
				Name="VCBscMakeTool"
			/>
			<Tool
				Name="VCFxCopTool"
			/>
			<Tool
				Name="VCPostBuildEventTool"
			/>
		</Configuration>
		<Configuration
			Name="Release|Win32"
			OutputDirectory="$rel_dir"
			ConfigurationType="4"
			CharacterSet="0"
			WholeProgramOptimization="1"
			IntermediateDirectory="\$(ProjectDir)\$(ConfigurationName)"
			>
			<Tool
				Name="VCPreBuildEventTool"
			/>
			<Tool
				Name="VCCustomBuildTool"
			/>
			<Tool
				Name="VCXMLDataGeneratorTool"
			/>
			<Tool
				Name="VCMIDLTool"
			/>
			<Tool
				Name="VCCLCompilerTool"
				AdditionalOptions="$cflags_release"
				Optimization="2"
				InlineFunctionExpansion="1"
				EnableIntrinsicFunctions="true"
				AdditionalIncludeDirectories="$includes"
				PreprocessorDefinitions="WIN32,NDEBUG,$defines"
				RuntimeLibrary="0"
				EnableFunctionLevelLinking="true"
				UsePrecompiledHeader="0"
				ProgramDataBaseFileName="\$(IntDir)\\\$(TargetName).pdb"
				WarningLevel="3"
				DebugInformationFormat="3"
			/>
			<Tool
				Name="VCManagedResourceCompilerTool"
			/>
			<Tool
				Name="VCResourceCompilerTool"
			/>
			<Tool
				Name="VCPreLinkEventTool"
			/>
			<Tool
				Name="VCLibrarianTool"
				SuppressStartupBanner="true"
			/>
			<Tool
				Name="VCALinkTool"
			/>
			<Tool
				Name="VCXDCMakeTool"
			/>
			<Tool
				Name="VCBscMakeTool"
			/>
			<Tool
				Name="VCFxCopTool"
			/>
			<Tool
				Name="VCPostBuildEventTool"
			/>
		</Configuration>
	</Configurations>
	<Files>
		<Filter
			Name="Source Files"
			Filter="cpp;c;cxx;def;odl;idl;hpj;bat;asm;asmx"
			UniqueIdentifier="{4FC737F1-C7A5-4376-A066-2A32D752A2FF}">
EOM
    foreach(@sources) {
        print F << "EOM";
			<File
				RelativePath="$_"/>
EOM
    }
    print F << "EOM";
		</Filter>
	</Files>
	<Globals>
	</Globals>
</VisualStudioProject>
EOM
    close F;
}

sub createAppProject {
    my ($appname, $git_dir, $out_dir, $rel_dir, $build_structure) = @_;
    print "Generate $appname vcproj app project\n";
    $rel_dir = "..\\$rel_dir";
    $rel_dir =~ s/\//\\/g;

    my $target = $appname;
    $target =~ s/\//_/g;
    $target =~ s/\.exe//;

    my $uuid = generate_guid($appname);
    $$build_structure{"APPS_${target}_GUID"} = $uuid;

    my @srcs = sort(map("$rel_dir\\$_", @{$$build_structure{"APPS_${appname}_SOURCES"}}));
    my @sources;
    foreach (@srcs) {
        $_ =~ s/\//\\/g;
        push(@sources, $_);
    }
    my $defines = join(",", sort(@{$$build_structure{"APPS_${appname}_DEFINES"}}));
    my $includes= join(";", sort(map("&quot;$rel_dir\\$_&quot;", @{$$build_structure{"APPS_${appname}_INCLUDES"}})));
    my $cflags  = join(" ", sort(@{$$build_structure{"APPS_${appname}_CFLAGS"}}));
    $cflags =~ s/\"/&quot;/g;
    $cflags =~ s/</&lt;/g;
    $cflags =~ s/>/&gt;/g;

    my $cflags_debug = $cflags;
    $cflags_debug =~ s/-MT/-MTd/;
    $cflags_debug =~ s/-O.//;

    my $cflags_release = $cflags;
    $cflags_release =~ s/-MTd/-MT/;

    my $libs;
    foreach (sort(@{$$build_structure{"APPS_${appname}_LIBS"}})) {
        $_ =~ s/\//_/g;
        $libs .= " $_";
    }
    my @tmp  = @{$$build_structure{"APPS_${appname}_LFLAGS"}};
    my @tmp2 = ();
    foreach (@tmp) {
        if (/^-LTCG/) {
        } elsif (/^-L/) {
            $_ =~ s/^-L/-LIBPATH:$rel_dir\//;
        }
        push(@tmp2, $_);
    }
    my $lflags = join(" ", sort(@tmp)) . " -LIBPATH:$rel_dir";

    $defines =~ s/-D//g;
    $defines =~ s/\"/\\&quot;/g;
    $defines =~ s/</&lt;/g;
    $defines =~ s/>/&gt;/g;
    $defines =~ s/\'//g;
    $defines =~ s/\\\\/\\/g;
    $includes =~ s/-I//g;
    mkdir "$target" || die "Could not create the directory $target for lib project!\n";
    open F, ">$target/$target.vcproj" || die "Could not open $target/$target.pro for writing!\n";
    binmode F, ":crlf";
    print F << "EOM";
<?xml version="1.0" encoding = "Windows-1252"?>
<VisualStudioProject
	ProjectType="Visual C++"
	Version="9,00"
	Name="$target"
	ProjectGUID="$uuid">
	<Platforms>
		<Platform
			Name="Win32"/>
	</Platforms>
	<ToolFiles>
	</ToolFiles>
	<Configurations>
		<Configuration
			Name="Debug|Win32"
			OutputDirectory="$rel_dir"
			ConfigurationType="1"
			CharacterSet="0"
			IntermediateDirectory="\$(ProjectDir)\$(ConfigurationName)"
			>
			<Tool
				Name="VCPreBuildEventTool"
			/>
			<Tool
				Name="VCCustomBuildTool"
			/>
			<Tool
				Name="VCXMLDataGeneratorTool"
			/>
			<Tool
				Name="VCMIDLTool"
			/>
			<Tool
				Name="VCCLCompilerTool"
				AdditionalOptions="$cflags_debug"
				Optimization="0"
				InlineFunctionExpansion="1"
				AdditionalIncludeDirectories="$includes"
				PreprocessorDefinitions="WIN32,_DEBUG,$defines"
				MinimalRebuild="true"
				RuntimeLibrary="1"
				UsePrecompiledHeader="0"
				ProgramDataBaseFileName="\$(IntDir)\\\$(TargetName).pdb"
				WarningLevel="3"
				DebugInformationFormat="3"
			/>
			<Tool
				Name="VCManagedResourceCompilerTool"
			/>
			<Tool
				Name="VCResourceCompilerTool"
			/>
			<Tool
				Name="VCPreLinkEventTool"
			/>
			<Tool
				Name="VCLinkerTool"
				AdditionalDependencies="$libs"
				AdditionalOptions="$lflags"
				LinkIncremental="2"
				GenerateDebugInformation="true"
				SubSystem="1"
				TargetMachine="1"
			/>
			<Tool
				Name="VCALinkTool"
			/>
			<Tool
				Name="VCXDCMakeTool"
			/>
			<Tool
				Name="VCBscMakeTool"
			/>
			<Tool
				Name="VCFxCopTool"
			/>
			<Tool
				Name="VCPostBuildEventTool"
			/>
		</Configuration>
		<Configuration
			Name="Release|Win32"
			OutputDirectory="$rel_dir"
			ConfigurationType="1"
			CharacterSet="0"
			WholeProgramOptimization="1"
			IntermediateDirectory="\$(ProjectDir)\$(ConfigurationName)"
			>
			<Tool
				Name="VCPreBuildEventTool"
			/>
			<Tool
				Name="VCCustomBuildTool"
			/>
			<Tool
				Name="VCXMLDataGeneratorTool"
			/>
			<Tool
				Name="VCMIDLTool"
			/>
			<Tool
				Name="VCCLCompilerTool"
				AdditionalOptions="$cflags_release"
				Optimization="2"
				InlineFunctionExpansion="1"
				EnableIntrinsicFunctions="true"
				AdditionalIncludeDirectories="$includes"
				PreprocessorDefinitions="WIN32,NDEBUG,$defines"
				RuntimeLibrary="0"
				EnableFunctionLevelLinking="true"
				UsePrecompiledHeader="0"
				ProgramDataBaseFileName="\$(IntDir)\\\$(TargetName).pdb"
				WarningLevel="3"
				DebugInformationFormat="3"
			/>
			<Tool
				Name="VCManagedResourceCompilerTool"
			/>
			<Tool
				Name="VCResourceCompilerTool"
			/>
			<Tool
				Name="VCPreLinkEventTool"
			/>
			<Tool
				Name="VCLinkerTool"
				AdditionalDependencies="$libs"
				AdditionalOptions="$lflags"
				LinkIncremental="1"
				GenerateDebugInformation="true"
				SubSystem="1"
				TargetMachine="1"
				OptimizeReferences="2"
				EnableCOMDATFolding="2"
			/>
			<Tool
				Name="VCALinkTool"
			/>
			<Tool
				Name="VCXDCMakeTool"
			/>
			<Tool
				Name="VCBscMakeTool"
			/>
			<Tool
				Name="VCFxCopTool"
			/>
			<Tool
				Name="VCPostBuildEventTool"
			/>
		</Configuration>
	</Configurations>
	<Files>
		<Filter
			Name="Source Files"
			Filter="cpp;c;cxx;def;odl;idl;hpj;bat;asm;asmx"
			UniqueIdentifier="{4FC737F1-C7A5-4376-A066-2A32D752A2FF}">
EOM
    foreach(@sources) {
        print F << "EOM";
			<File
				RelativePath="$_"/>
EOM
    }
    print F << "EOM";
		</Filter>
	</Files>
	<Globals>
	</Globals>
</VisualStudioProject>
EOM
    close F;
}

sub createGlueProject {
    my ($git_dir, $out_dir, $rel_dir, %build_structure) = @_;
    print "Generate solutions file\n";
    $rel_dir = "..\\$rel_dir";
    $rel_dir =~ s/\//\\/g;
    my $SLN_HEAD = "Microsoft Visual Studio Solution File, Format Version 10.00\n# Visual Studio 2008\n";
    my $SLN_PRE  = "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = ";
    my $SLN_POST = "\nEndProject\n";

    my @libs = @{$build_structure{"LIBS"}};
    my @tmp;
    foreach (@libs) {
        $_ =~ s/\//_/g;
        $_ =~ s/\.a//;
        push(@tmp, $_);
    }
    @libs = @tmp;

    my @apps = @{$build_structure{"APPS"}};
    @tmp = ();
    foreach (@apps) {
        $_ =~ s/\//_/g;
        $_ =~ s/\.exe//;
        if ($_ eq "git" ) {
            unshift(@tmp, $_);
        } else {
            push(@tmp, $_);
        }
    }
    @apps = @tmp;

    open F, ">git.sln" || die "Could not open git.sln for writing!\n";
    binmode F, ":crlf";
    print F "$SLN_HEAD";

    my $uuid_libgit = $build_structure{"LIBS_libgit_GUID"};
    my $uuid_xdiff_lib = $build_structure{"LIBS_xdiff_lib_GUID"};
    foreach (@apps) {
        my $appname = $_;
        my $uuid = $build_structure{"APPS_${appname}_GUID"};
        print F "$SLN_PRE";
        print F "\"${appname}\", \"${appname}\\${appname}.vcproj\", \"${uuid}\"\n";
        print F "	ProjectSection(ProjectDependencies) = postProject\n";
        print F "		${uuid_libgit} = ${uuid_libgit}\n";
        print F "		${uuid_xdiff_lib} = ${uuid_xdiff_lib}\n";
        print F "	EndProjectSection";
        print F "$SLN_POST";
    }
    foreach (@libs) {
        my $libname = $_;
        my $uuid = $build_structure{"LIBS_${libname}_GUID"};
        print F "$SLN_PRE";
        print F "\"${libname}\", \"${libname}\\${libname}.vcproj\", \"${uuid}\"";
        print F "$SLN_POST";
    }

    print F << "EOM";
Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
		Debug|Win32 = Debug|Win32
		Release|Win32 = Release|Win32
	EndGlobalSection
EOM
    print F << "EOM";
	GlobalSection(ProjectConfigurationPlatforms) = postSolution
EOM
    foreach (@apps) {
        my $appname = $_;
        my $uuid = $build_structure{"APPS_${appname}_GUID"};
        print F "\t\t${uuid}.Debug|Win32.ActiveCfg = Debug|Win32\n";
        print F "\t\t${uuid}.Debug|Win32.Build.0 = Debug|Win32\n";
        print F "\t\t${uuid}.Release|Win32.ActiveCfg = Release|Win32\n";
        print F "\t\t${uuid}.Release|Win32.Build.0 = Release|Win32\n";
    }
    foreach (@libs) {
        my $libname = $_;
        my $uuid = $build_structure{"LIBS_${libname}_GUID"};
        print F "\t\t${uuid}.Debug|Win32.ActiveCfg = Debug|Win32\n";
        print F "\t\t${uuid}.Debug|Win32.Build.0 = Debug|Win32\n";
        print F "\t\t${uuid}.Release|Win32.ActiveCfg = Release|Win32\n";
        print F "\t\t${uuid}.Release|Win32.Build.0 = Release|Win32\n";
    }

    print F << "EOM";
	EndGlobalSection
EndGlobal
EOM
    close F;
}

1;
