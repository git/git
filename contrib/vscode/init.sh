#!/bin/sh

die () {
	echo "$*" >&2
	exit 1
}

cd "$(dirname "$0")"/../.. ||
die "Could not cd to top-level directory"

mkdir -p .vscode ||
die "Could not create .vscode/"

# General settings

cat >.vscode/settings.json.new <<\EOF ||
{
    "C_Cpp.intelliSenseEngine": "Default",
    "C_Cpp.intelliSenseEngineFallback": "Disabled",
    "[git-commit]": {
        "editor.wordWrap": "wordWrapColumn",
        "editor.wordWrapColumn": 72
    },
    "files.associations": {
        "*.h": "c",
        "*.c": "c"
    }
}
EOF
die "Could not write settings.json"

# Infer some setup-specific locations/names

GCCPATH="$(which gcc)"
GDBPATH="$(which gdb)"
MAKECOMMAND="make -j5 DEVELOPER=1"
OSNAME=
X=
case "$(uname -s)" in
MINGW*)
	GCCPATH="$(cygpath -am "$GCCPATH")"
	GDBPATH="$(cygpath -am "$GDBPATH")"
	MAKE_BASH="$(cygpath -am /git-cmd.exe) --command=usr\\\\bin\\\\bash.exe"
	MAKECOMMAND="$MAKE_BASH -lc \\\"$MAKECOMMAND\\\""
	OSNAME=Win32
	X=.exe
	;;
Linux)
	OSNAME=Linux
	;;
Darwin)
	OSNAME=macOS
	;;
esac

# Default build task

cat >.vscode/tasks.json.new <<EOF ||
{
    // See https://go.microsoft.com/fwlink/?LinkId=733558
    // for the documentation about the tasks.json format
    "version": "2.0.0",
    "tasks": [
        {
            "label": "make",
            "type": "shell",
            "command": "$MAKECOMMAND",
            "group": {
                "kind": "build",
                "isDefault": true
            }
        }
    ]
}
EOF
die "Could not install default build task"

# Debugger settings

cat >.vscode/launch.json.new <<EOF ||
{
    // Use IntelliSense to learn about possible attributes.
    // Hover to view descriptions of existing attributes.
    // For more information, visit:
    // https://go.microsoft.com/fwlink/?linkid=830387
    "version": "0.2.0",
    "configurations": [
        {
            "name": "(gdb) Launch",
            "type": "cppdbg",
            "request": "launch",
            "program": "\${workspaceFolder}/git$X",
            "args": [],
            "stopAtEntry": false,
            "cwd": "\${workspaceFolder}",
            "environment": [],
            "externalConsole": true,
            "MIMode": "gdb",
            "miDebuggerPath": "$GDBPATH",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ]
        }
    ]
}
EOF
die "Could not write launch configuration"

# C/C++ extension settings

make -f - OSNAME=$OSNAME GCCPATH="$GCCPATH" vscode-init \
	>.vscode/c_cpp_properties.json <<\EOF ||
include Makefile

vscode-init:
	@mkdir -p .vscode && \
	incs= && defs= && \
	for e in $(ALL_CFLAGS) \
			'-DGIT_EXEC_PATH="$(gitexecdir_SQ)"' \
			'-DGIT_LOCALE_PATH="$(localedir_relative_SQ)"' \
			'-DBINDIR="$(bindir_relative_SQ)"' \
			'-DFALLBACK_RUNTIME_PREFIX="$(prefix_SQ)"' \
			'-DDEFAULT_GIT_TEMPLATE_DIR="$(template_dir_SQ)"' \
			'-DETC_GITCONFIG="$(ETC_GITCONFIG_SQ)"' \
			'-DETC_GITATTRIBUTES="$(ETC_GITATTRIBUTES_SQ)"' \
			'-DGIT_LOCALE_PATH="$(localedir_relative_SQ)"' \
			'-DCURL_DISABLE_TYPECHECK', \
			'-DGIT_HTML_PATH="$(htmldir_relative_SQ)"' \
			'-DGIT_MAN_PATH="$(mandir_relative_SQ)"' \
			'-DGIT_INFO_PATH="$(infodir_relative_SQ)"'; do \
		case "$$e" in \
		-I.) \
			incs="$$(printf '% 16s"$${workspaceRoot}",\n%s' \
				"" "$$incs")" \
			;; \
		-I/*) \
			incs="$$(printf '% 16s"%s",\n%s' \
				"" "$${e#-I}" "$$incs")" \
			;; \
		-I*) \
			incs="$$(printf '% 16s"$${workspaceRoot}/%s",\n%s' \
				"" "$${e#-I}" "$$incs")" \
			;; \
		-D*) \
			defs="$$(printf '% 16s"%s",\n%s' \
				"" "$$(echo "$${e#-D}" | sed 's/"/\\&/g')" \
				"$$defs")" \
			;; \
		esac; \
	done && \
	echo '{' && \
	echo '    "configurations": [' && \
	echo '        {' && \
	echo '            "name": "$(OSNAME)",' && \
	echo '            "intelliSenseMode": "clang-x64",' && \
	echo '            "includePath": [' && \
	echo "$$incs" | sort | sed '$$s/,$$//' && \
	echo '            ],' && \
	echo '            "defines": [' && \
	echo "$$defs" | sort | sed '$$s/,$$//' && \
	echo '            ],' && \
	echo '            "browse": {' && \
	echo '                "limitSymbolsToIncludedHeaders": true,' && \
	echo '                "databaseFilename": "",' && \
	echo '                "path": [' && \
	echo '                    "$${workspaceRoot}"' && \
	echo '                ]' && \
	echo '            },' && \
	echo '            "cStandard": "c11",' && \
	echo '            "cppStandard": "c++17",' && \
	echo '            "compilerPath": "$(GCCPATH)"' && \
	echo '        }' && \
	echo '    ],' && \
	echo '    "version": 4' && \
	echo '}'
EOF
die "Could not write settings for the C/C++ extension"

for file in .vscode/settings.json .vscode/tasks.json .vscode/launch.json
do
	if test -f $file
	then
		if git diff --no-index --quiet --exit-code $file $file.new
		then
			rm $file.new
		else
			printf "The file $file.new has these changes:\n\n"
			git --no-pager diff --no-index $file $file.new
			printf "\n\nMaybe \`mv $file.new $file\`?\n\n"
		fi
	else
		mv $file.new $file
	fi
done
