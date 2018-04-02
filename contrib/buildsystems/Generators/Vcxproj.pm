package Generators::Vcxproj;
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
        createProject($_, $git_dir, $out_dir, $rel_dir, \%build_structure, 1);
    }

    my @apps = @{$build_structure{"APPS"}};
    foreach (@apps) {
        createProject($_, $git_dir, $out_dir, $rel_dir, \%build_structure, 0);
    }

    createGlueProject($git_dir, $out_dir, $rel_dir, %build_structure);
    return 0;
}

sub createProject {
    my ($name, $git_dir, $out_dir, $rel_dir, $build_structure, $static_library) = @_;
    my $label = $static_library ? "lib" : "app";
    my $prefix = $static_library ? "LIBS_" : "APPS_";
    my $config_type = $static_library ? "StaticLibrary" : "Application";
    print "Generate $name vcxproj $label project\n";
    my $cdup = $name;
    $cdup =~ s/[^\/]+/../g;
    $cdup =~ s/\//\\/g;
    $rel_dir = $rel_dir eq "." ? $cdup : "$cdup\\$rel_dir";
    $rel_dir =~ s/\//\\/g;

    my $target = $name;
    if ($static_library) {
      $target =~ s/\.a//;
    } else {
      $target =~ s/\.exe//;
    }

    my $uuid = $GUIDS[$guid_index++];
    $$build_structure{"$prefix${target}_GUID"} = $uuid;
    my $vcxproj = $target;
    $vcxproj =~ s/(.*\/)?(.*)/$&\/$2.vcxproj/;
    $vcxproj =~ s/([^\/]*)(\/lib)\/(lib.vcxproj)/$1$2\/$1_$3/;
    $$build_structure{"$prefix${target}_VCXPROJ"} = $vcxproj;

    my @srcs = sort(map("$rel_dir\\$_", @{$$build_structure{"$prefix${name}_SOURCES"}}));
    my @sources;
    foreach (@srcs) {
        $_ =~ s/\//\\/g;
        push(@sources, $_);
    }
    my $defines = join(";", sort(@{$$build_structure{"$prefix${name}_DEFINES"}}));
    my $includes= join(";", sort(map { s/^-I//; s/\//\\/g; File::Spec->file_name_is_absolute($_) ? $_ : "$rel_dir\\$_" } @{$$build_structure{"$prefix${name}_INCLUDES"}}));
    my $cflags = join(" ", sort(map { s/^-[GLMOZ].*//; s/.* .*/"$&"/; $_; } @{$$build_structure{"$prefix${name}_CFLAGS"}}));
    $cflags =~ s/\"/&quot;/g;
    $cflags =~ s/</&lt;/g;
    $cflags =~ s/>/&gt;/g;

    my $libs = '';
    if (!$static_library) {
      $libs = join(";", sort(grep /^(?!libgit\.lib|xdiff\/lib\.lib|vcs-svn\/lib\.lib|libcurl\.lib|libeay32\.lib|libiconv\.lib|ssleay32\.lib|zlib\.lib)/, @{$$build_structure{"$prefix${name}_LIBS"}}));
    }

    $defines =~ s/-D//g;
    $defines =~ s/\"/&quot;/g;
    $defines =~ s/</&lt;/g;
    $defines =~ s/>/&gt;/g;
    $defines =~ s/\'//g;

    die "Could not create the directory $target for $label project!\n" unless (-d "$target" || mkdir "$target");

    use File::Copy;
    copy("$git_dir/compat/vcbuild/packages.config", "$target/packages.config");

    my $needsCurl = grep(/libcurl.lib/, @{$$build_structure{"$prefix${name}_LIBS"}});
    my $targetsImport = '';
    my $targetsErrors = '';
    my $afterTargets = '';
    open F, "<$git_dir/compat/vcbuild/packages.config";
    while (<F>) {
      if (/<package id="([^"]+)" version="([^"]+)"/) {
        if ($1 eq 'libiconv') {
	  # we have to link with the Release builds already because libiconv
	  # is only available targeting v100 and v110, see
	  # https://github.com/coapp-packages/libiconv/issues/2
          $libs .= ";$rel_dir\\compat\\vcbuild\\GEN.PKGS\\$1.$2\\build\\native\\lib\\v110\\\$(Platform)\\Release\\dynamic\\cdecl\\libiconv.lib";
	  $afterTargets .= "\n    <Copy SourceFiles=\"$rel_dir\\compat\\vcbuild\\GEN.PKGS\\$1.redist.$2\\build\\native\\bin\\v110\\\$(Platform)\\Release\\dynamic\\cdecl\\libiconv.dll\" DestinationFolder=\"\$(TargetDir)\" SkipUnchangedFiles=\"true\" />";
        } elsif ($needsCurl && $1 eq 'curl') {
	  # libcurl is only available targeting v100 and v110
	  $libs .= ";$rel_dir\\compat\\vcbuild\\GEN.PKGS\\$1.$2\\build\\native\\lib\\v110\\\$(Platform)\\Release\\dynamic\\libcurl.lib";
	  $afterTargets .= "\n    <Copy SourceFiles=\"$rel_dir\\compat\\vcbuild\\GEN.PKGS\\$1.redist.$2\\build\\native\\bin\\v110\\\$(Platform)\\Release\\dynamic\\libcurl.dll\" DestinationFolder=\"\$(TargetDir)\" SkipUnchangedFiles=\"true\" />";
        } elsif ($needsCurl && $1 eq 'expat') {
	  # libexpat is only available targeting v100 and v110
	  $libs .= ";$rel_dir\\compat\\vcbuild\\GEN.PKGS\\$1.$2\\build\\native\\lib\\v110\\\$(Platform)\\Release\\dynamic\\utf8\\libexpat.lib";
	}
        next if ($1 =~  /^(zlib$|openssl(?!.*(x64|x86)$))/);
        my $targetsFile = "$rel_dir\\compat\\vcbuild\\GEN.PKGS\\$1.$2\\build\\native\\$1.targets";
        $targetsImport .= "\n    <Import Project=\"$targetsFile\" Condition=\"Exists('$targetsFile')\" />";
        $targetsErrors .= "\n    <Error Condition=\"!Exists('$targetsFile')\" Text=\"\$([System.String]::Format('\$(ErrorText)', '$targetsFile'))\" />";
      }
    }
    close F;

    open F, ">$vcxproj" or die "Could not open $vcxproj for writing!\n";
    binmode F, ":crlf :utf8";
    print F chr(0xFEFF);
    print F << "EOM";
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="14.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|Win32">
      <Configuration>Debug</Configuration>
      <Platform>Win32</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Release|Win32">
      <Configuration>Release</Configuration>
      <Platform>Win32</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Debug|x64">
      <Configuration>Debug</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64">
      <Configuration>Release</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <ProjectGuid>$uuid</ProjectGuid>
    <Keyword>Win32Proj</Keyword>
  </PropertyGroup>
  <Import Project="\$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'\$(Configuration)'=='Debug'" Label="Configuration">
    <UseDebugLibraries>true</UseDebugLibraries>
    <LinkIncremental>true</LinkIncremental>
  </PropertyGroup>
  <PropertyGroup Condition="'\$(Configuration)'=='Release'" Label="Configuration">
    <UseDebugLibraries>false</UseDebugLibraries>
    <WholeProgramOptimization>true</WholeProgramOptimization>
  </PropertyGroup>
  <PropertyGroup>
    <ConfigurationType>$config_type</ConfigurationType>
    <PlatformToolset>v140</PlatformToolset>
    <!-- <CharacterSet>UTF-8</CharacterSet> -->
    <OutDir>..\\</OutDir>
    <!-- <IntDir>\$(ProjectDir)\$(Configuration)\\</IntDir> -->
  </PropertyGroup>
  <Import Project="\$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <ImportGroup Label="ExtensionSettings">
  </ImportGroup>
  <ImportGroup Label="Shared">
  </ImportGroup>
  <ImportGroup Label="PropertySheets">
    <Import Project="\$(UserRootDir)\\Microsoft.Cpp.\$(Platform).user.props" Condition="exists('\$(UserRootDir)\\Microsoft.Cpp.\$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
  <PropertyGroup Label="UserMacros" />
  <PropertyGroup>
    <GenerateManifest>false</GenerateManifest>
    <EnableManagedIncrementalBuild>true</EnableManagedIncrementalBuild>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalOptions>$cflags %(AdditionalOptions)</AdditionalOptions>
      <AdditionalIncludeDirectories>$includes;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <EnableParallelCodeGeneration />
      <MinimalRebuild>true</MinimalRebuild>
      <InlineFunctionExpansion>OnlyExplicitInline</InlineFunctionExpansion>
      <PrecompiledHeader />
      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>
    </ClCompile>
    <Lib>
      <SuppressStartupBanner>true</SuppressStartupBanner>
    </Lib>
    <Link>
      <AdditionalDependencies>$libs;\$(AdditionalDependencies)</AdditionalDependencies>
      <AdditionalOptions>invalidcontinue.obj %(AdditionalOptions)</AdditionalOptions>
      <ManifestFile>$cdup\\compat\\win32\\git.manifest</ManifestFile>
      <SubSystem>Console</SubSystem>
    </Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'\$(Platform)'=='Win32'">
    <Link>
      <TargetMachine>MachineX86</TargetMachine>
    </Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'\$(Configuration)'=='Debug'">
    <ClCompile>
      <Optimization>Disabled</Optimization>
      <PreprocessorDefinitions>WIN32;_DEBUG;$defines;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary>
    </ClCompile>
    <Link>
      <GenerateDebugInformation>true</GenerateDebugInformation>
    </Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'\$(Configuration)'=='Release'">
    <ClCompile>
      <Optimization>MaxSpeed</Optimization>
      <IntrinsicFunctions>true</IntrinsicFunctions>
      <PreprocessorDefinitions>WIN32;NDEBUG;$defines;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>
      <FunctionLevelLinking>true</FunctionLevelLinking>
      <FavorSizeOrSpeed>Speed</FavorSizeOrSpeed>
    </ClCompile>
    <Link>
      <GenerateDebugInformation>true</GenerateDebugInformation>
      <EnableCOMDATFolding>true</EnableCOMDATFolding>
      <OptimizeReferences>true</OptimizeReferences>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
EOM
    foreach(@sources) {
        print F << "EOM";
    <ClCompile Include="$_" />
EOM
    }
    print F << "EOM";
  </ItemGroup>
EOM
    if (!$static_library) {
      my $uuid_libgit = $$build_structure{"LIBS_libgit_GUID"};
      my $uuid_xdiff_lib = $$build_structure{"LIBS_xdiff/lib_GUID"};

      print F << "EOM";
  <ItemGroup>
    <ProjectReference Include="$cdup\\libgit\\libgit.vcxproj">
      <Project>$uuid_libgit</Project>
      <ReferenceOutputAssembly>false</ReferenceOutputAssembly>
    </ProjectReference>
    <ProjectReference Include="$cdup\\xdiff\\lib\\xdiff_lib.vcxproj">
      <Project>$uuid_xdiff_lib</Project>
      <ReferenceOutputAssembly>false</ReferenceOutputAssembly>
    </ProjectReference>
EOM
      if ($name =~ /(test-(line-buffer|svn-fe)|^git-remote-testsvn)\.exe$/) {
        my $uuid_vcs_svn_lib = $$build_structure{"LIBS_vcs-svn/lib_GUID"};
        print F << "EOM";
    <ProjectReference Include="$cdup\\vcs-svn\\lib\\vcs-svn_lib.vcxproj">
      <Project>$uuid_vcs_svn_lib</Project>
      <ReferenceOutputAssembly>false</ReferenceOutputAssembly>
    </ProjectReference>
EOM
      }
      print F << "EOM";
  </ItemGroup>
EOM
    }
    print F << "EOM";
  <ItemGroup>
    <None Include="packages.config" />
  </ItemGroup>
  <Import Project="\$(VCTargetsPath)\\Microsoft.Cpp.targets" />
  <ImportGroup Label="ExtensionTargets">$targetsImport
  </ImportGroup>
  <Target Name="EnsureNuGetPackageBuildImports" BeforeTargets="PrepareForBuild">
    <PropertyGroup>
      <ErrorText>This project references NuGet package(s) that are missing on this computer. Use NuGet Package Restore to download them.  For more information, see http://go.microsoft.com/fwlink/?LinkID=322105. The missing file is {0}.</ErrorText>
    </PropertyGroup>$targetsErrors
  </Target>
EOM
    if (!$static_library && $afterTargets ne '') {
      print F << "EOM";
  <Target Name="${target}_AfterBuild" AfterTargets="AfterBuild">$afterTargets
  </Target>
EOM
    }
    print F << "EOM";
</Project>
EOM
    close F;
}

sub createGlueProject {
    my ($git_dir, $out_dir, $rel_dir, %build_structure) = @_;
    print "Generate solutions file\n";
    $rel_dir = "..\\$rel_dir";
    $rel_dir =~ s/\//\\/g;
    my $SLN_HEAD = "Microsoft Visual Studio Solution File, Format Version 11.00\n# Visual Studio 2010\n";
    my $SLN_PRE  = "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = ";
    my $SLN_POST = "\nEndProject\n";

    my @libs = @{$build_structure{"LIBS"}};
    my @tmp;
    foreach (@libs) {
        $_ =~ s/\.a//;
        push(@tmp, $_);
    }
    @libs = @tmp;

    my @apps = @{$build_structure{"APPS"}};
    @tmp = ();
    foreach (@apps) {
        $_ =~ s/\.exe//;
        if ($_ eq "git" ) {
            unshift(@tmp, $_);
        } else {
            push(@tmp, $_);
        }
    }
    @apps = @tmp;

    open F, ">git.sln" || die "Could not open git.sln for writing!\n";
    binmode F, ":crlf :utf8";
    print F chr(0xFEFF);
    print F "$SLN_HEAD";

    foreach (@apps) {
        my $appname = $_;
        my $uuid = $build_structure{"APPS_${appname}_GUID"};
        print F "$SLN_PRE";
	my $vcxproj = $build_structure{"APPS_${appname}_VCXPROJ"};
	$vcxproj =~ s/\//\\/g;
        $appname =~ s/.*\///;
        print F "\"${appname}\", \"${vcxproj}\", \"${uuid}\"";
        print F "$SLN_POST";
    }
    foreach (@libs) {
        my $libname = $_;
        my $uuid = $build_structure{"LIBS_${libname}_GUID"};
        print F "$SLN_PRE";
        my $vcxproj = $build_structure{"LIBS_${libname}_VCXPROJ"};
	$vcxproj =~ s/\//\\/g;
        $libname =~ s/\//_/g;
        print F "\"${libname}\", \"${vcxproj}\", \"${uuid}\"";
        print F "$SLN_POST";
    }

    print F << "EOM";
Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
		Debug|x64 = Debug|x64
		Debug|x86 = Debug|x86
		Release|x64 = Release|x64
		Release|x86 = Release|x86
	EndGlobalSection
EOM
    print F << "EOM";
	GlobalSection(ProjectConfigurationPlatforms) = postSolution
EOM
    foreach (@apps) {
        my $appname = $_;
        my $uuid = $build_structure{"APPS_${appname}_GUID"};
        print F "\t\t${uuid}.Debug|x64.ActiveCfg = Debug|x64\n";
        print F "\t\t${uuid}.Debug|x64.Build.0 = Debug|x64\n";
        print F "\t\t${uuid}.Debug|x86.ActiveCfg = Debug|Win32\n";
        print F "\t\t${uuid}.Debug|x86.Build.0 = Debug|Win32\n";
        print F "\t\t${uuid}.Release|x64.ActiveCfg = Release|x64\n";
        print F "\t\t${uuid}.Release|x64.Build.0 = Release|x64\n";
        print F "\t\t${uuid}.Release|x86.ActiveCfg = Release|Win32\n";
        print F "\t\t${uuid}.Release|x86.Build.0 = Release|Win32\n";
    }
    foreach (@libs) {
        my $libname = $_;
        my $uuid = $build_structure{"LIBS_${libname}_GUID"};
        print F "\t\t${uuid}.Debug|x64.ActiveCfg = Debug|x64\n";
        print F "\t\t${uuid}.Debug|x64.Build.0 = Debug|x64\n";
        print F "\t\t${uuid}.Debug|x86.ActiveCfg = Debug|Win32\n";
        print F "\t\t${uuid}.Debug|x86.Build.0 = Debug|Win32\n";
        print F "\t\t${uuid}.Release|x64.ActiveCfg = Release|x64\n";
        print F "\t\t${uuid}.Release|x64.Build.0 = Release|x64\n";
        print F "\t\t${uuid}.Release|x86.ActiveCfg = Release|Win32\n";
        print F "\t\t${uuid}.Release|x86.Build.0 = Release|Win32\n";
    }

    print F << "EOM";
	EndGlobalSection
	GlobalSection(SolutionProperties) = preSolution
		HideSolutionNode = FALSE
	EndGlobalSection
EndGlobal
EOM
    close F;
}

1;
