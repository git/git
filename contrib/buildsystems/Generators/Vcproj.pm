package Generators::Vcproj;
require Exporter;

use strict;
use vars qw($VERSION);

our $VERSION = '1.00';
our(@ISA, @EXPORT, @EXPORT_OK, @AVAILABLE);
@ISA = qw(Exporter);

BEGIN {
    push @EXPORT_OK, qw(generate);
}

my $guid_index = 0;
my @GUIDS = (
    "{E07B9989-2BF7-4F21-8918-BE22BA467AC3}",
    "{278FFB51-0296-4A44-A81A-22B87B7C3592}",
    "{7346A2C4-F0FD-444F-9EBE-1AF23B2B5650}",
    "{67F421AC-EB34-4D49-820B-3196807B423F}",
    "{385DCFE1-CC8C-4211-A451-80FCFC31CA51}",
    "{97CC46C5-D2CC-4D26-B634-E75792B79916}",
    "{C7CE21FE-6EF8-4012-A5C7-A22BCEDFBA11}",
    "{51575134-3FDF-42D1-BABD-3FB12669C6C9}",
    "{0AE195E4-9823-4B87-8E6F-20C5614AF2FF}",
    "{4B918255-67CA-43BB-A46C-26704B666E6B}",
    "{18CCFEEF-C8EE-4CC1-A265-26F95C9F4649}",
    "{5D5D90FA-01B7-4973-AFE5-CA88C53AC197}",
    "{1F054320-036D-49E1-B384-FB5DF0BC8AC0}",
    "{7CED65EE-F2D9-4171-825B-C7D561FE5786}",
    "{8D341679-0F07-4664-9A56-3BA0DE88B9BC}",
    "{C189FEDC-2957-4BD7-9FA4-7622241EA145}",
    "{66844203-1B9F-4C53-9274-164FFF95B847}",
    "{E4FEA145-DECC-440D-AEEA-598CF381FD43}",
    "{73300A8E-C8AC-41B0-B555-4F596B681BA7}",
    "{873FDEB1-D01D-40BF-A1BF-8BBC58EC0F51}",
    "{7922C8BE-76C5-4AC6-8BF7-885C0F93B782}",
    "{E245D370-308B-4A49-BFC1-1E527827975F}",
    "{F6FA957B-66FC-4ED7-B260-E59BBE4FE813}",
    "{E6055070-0198-431A-BC49-8DB6CEE770AE}",
    "{54159234-C3EB-43DA-906B-CE5DA5C74654}",
    "{594CFC35-0B60-46F6-B8EF-9983ACC1187D}",
    "{D93FCAB7-1F01-48D2-B832-F761B83231A5}",
    "{DBA5E6AC-E7BE-42D3-8703-4E787141526E}",
    "{6171953F-DD26-44C7-A3BE-CC45F86FC11F}",
    "{9E19DDBE-F5E4-4A26-A2FE-0616E04879B8}",
    "{AE81A615-99E3-4885-9CE0-D9CAA193E867}",
    "{FBF4067E-1855-4F6C-8BCD-4D62E801A04D}",
    "{17007948-6593-4AEB-8106-F7884B4F2C19}",
    "{199D4C8D-8639-4DA6-82EF-08668C35DEE0}",
    "{E085E50E-C140-4CF3-BE4B-094B14F0DDD6}",
    "{00785268-A9CC-4E40-AC29-BAC0019159CE}",
    "{4C06F56A-DCDB-46A6-B67C-02339935CF12}",
    "{3A62D3FD-519E-4EC9-8171-D2C1BFEA022F}",
    "{9392EB58-D7BA-410B-B1F0-B2FAA6BC89A7}",
    "{2ACAB2D5-E0CE-4027-BCA0-D78B2D7A6C66}",
    "{86E216C3-43CE-481A-BCB2-BE5E62850635}",
    "{FB631291-7923-4B91-9A57-7B18FDBB7A42}",
    "{0A176EC9-E934-45B8-B87F-16C7F4C80039}",
    "{DF55CA80-46E8-4C53-B65B-4990A23DD444}",
    "{3A0F9895-55D2-4710-BE5E-AD7498B5BF44}",
    "{294BDC5A-F448-48B6-8110-DD0A81820F8C}",
    "{4B9F66E9-FAC9-47AB-B1EF-C16756FBFD06}",
    "{72EA49C6-2806-48BD-B81B-D4905102E19C}",
    "{5728EB7E-8929-486C-8CD5-3238D060E768}",
    "{A3E300FC-5630-4850-A470-E9F2C2EFA7E7}",
    "{CEA071D4-D9F3-4250-98F7-44AFDC8ACAA1}",
    "{3FD87BB4-2236-4A1B-ADD2-46211A302442}",
    "{49B03F41-5157-4079-95A7-64D728BCF74F}",
    "{95D5A28B-80E2-40A9-BEA3-C52B9CA488E3}",
    "{B85E6545-D523-4323-9F29-45389D090343}",
    "{06840CEF-746C-4B71-9442-C395DD6590A5}"
);

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

    my $uuid = $GUIDS[$guid_index];
    $$build_structure{"LIBS_${target}_GUID"} = $uuid;
    $guid_index += 1;

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
				Name="VCWebServiceProxyGeneratorTool"
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
				Name="VCWebServiceProxyGeneratorTool"
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

    my $uuid = $GUIDS[$guid_index];
    $$build_structure{"APPS_${target}_GUID"} = $uuid;
    $guid_index += 1;

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
				Name="VCWebServiceProxyGeneratorTool"
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
				Name="VCWebServiceProxyGeneratorTool"
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
