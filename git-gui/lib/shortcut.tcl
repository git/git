# git-gui desktop icon creators
# Copyright (C) 2006, 2007 Shawn Pearce

proc do_windows_shortcut {} {
	global argv0

	set fn [tk_getSaveFile \
		-parent . \
		-title "[appname] ([reponame]): Create Desktop Icon" \
		-initialfile "Git [reponame].bat"]
	if {$fn != {}} {
		if {[file extension $fn] ne {.bat}} {
			set fn ${fn}.bat
		}
		if {[catch {
				set ge [file normalize [file dirname $::_git]]
				set fd [open $fn w]
				puts $fd "@ECHO Entering [reponame]"
				puts $fd "@ECHO Starting git-gui... please wait..."
				puts $fd "@SET PATH=$ge;%PATH%"
				puts $fd "@SET GIT_DIR=[file normalize [gitdir]]"
				puts -nonewline $fd "@\"[info nameofexecutable]\""
				puts $fd " \"[file normalize $argv0]\""
				close $fd
			} err]} {
			error_popup "Cannot write script:\n\n$err"
		}
	}
}

proc do_cygwin_shortcut {} {
	global argv0

	if {[catch {
		set desktop [exec cygpath \
			--windows \
			--absolute \
			--long-name \
			--desktop]
		}]} {
			set desktop .
	}
	set fn [tk_getSaveFile \
		-parent . \
		-title "[appname] ([reponame]): Create Desktop Icon" \
		-initialdir $desktop \
		-initialfile "Git [reponame].bat"]
	if {$fn != {}} {
		if {[file extension $fn] ne {.bat}} {
			set fn ${fn}.bat
		}
		if {[catch {
				set fd [open $fn w]
				set sh [exec cygpath \
					--windows \
					--absolute \
					/bin/sh.exe]
				set me [exec cygpath \
					--unix \
					--absolute \
					$argv0]
				set gd [exec cygpath \
					--unix \
					--absolute \
					[gitdir]]
				puts $fd "@ECHO Entering [reponame]"
				puts $fd "@ECHO Starting git-gui... please wait..."
				puts -nonewline $fd "@\"$sh\" --login -c \""
				puts -nonewline $fd "GIT_DIR=[sq $gd]"
				puts -nonewline $fd " [sq $me]"
				puts $fd " &\""
				close $fd
			} err]} {
			error_popup "Cannot write script:\n\n$err"
		}
	}
}

proc do_macosx_app {} {
	global argv0 env

	set fn [tk_getSaveFile \
		-parent . \
		-title "[appname] ([reponame]): Create Desktop Icon" \
		-initialdir [file join $env(HOME) Desktop] \
		-initialfile "Git [reponame].app"]
	if {$fn != {}} {
		if {[file extension $fn] ne {.app}} {
			set fn ${fn}.app
		}
		if {[catch {
				set Contents [file join $fn Contents]
				set MacOS [file join $Contents MacOS]
				set exe [file join $MacOS git-gui]

				file mkdir $MacOS

				set fd [open [file join $Contents Info.plist] w]
				puts $fd {<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>English</string>
	<key>CFBundleExecutable</key>
	<string>git-gui</string>
	<key>CFBundleIdentifier</key>
	<string>org.spearce.git-gui</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleVersion</key>
	<string>1.0</string>
	<key>NSPrincipalClass</key>
	<string>NSApplication</string>
</dict>
</plist>}
				close $fd

				set fd [open $exe w]
				puts $fd "#!/bin/sh"
				foreach name [lsort [array names env]] {
					set value $env($name)
					switch -- $name {
					GIT_DIR { set value [file normalize [gitdir]] }
					}

					switch -glob -- $name {
					SSH_* -
					GIT_* {
						puts $fd "if test \"z\$$name\" = z; then"
						puts $fd "  export $name=[sq $value]"
						puts $fd "fi &&"
					}
					}
				}
				puts $fd "export PATH=[sq [file dirname $::_git]]:\$PATH &&"
				puts $fd "cd [sq [file normalize [pwd]]] &&"
				puts $fd "exec \\"
				puts $fd " [sq [info nameofexecutable]] \\"
				puts $fd " [sq [file normalize $argv0]]"
				close $fd

				file attributes $exe -permissions u+x,g+x,o+x
			} err]} {
			error_popup "Cannot write icon:\n\n$err"
		}
	}
}
