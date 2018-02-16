package Generators::Vcxproj;
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

    my $uuid = generate_guid($name);
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
