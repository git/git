# git-gui desktop icon creators
# Copyright (C) 2006, 2007 Shawn Pearce

proc do_windows_shortcut {} {
	global argv0

	set fn [tk_getSaveFile \
		-parent . \
		-title "[appname] ([reponame]): Create Desktop Icon" \
		-initialfile "Git [reponame].bat"]
	if {$fn != {}} {
		if {[catch {
				set fd [open $fn w]
				puts $fd "@ECHO Entering [reponame]"
				puts $fd "@ECHO Starting git-gui... please wait..."
				puts $fd "@SET PATH=[file normalize [gitexec]];%PATH%"
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
		if {[catch {
				set fd [open $fn w]
				set sh [exec cygpath \
					--windows \
					--absolute \
					/bin/sh]
				set me [exec cygpath \
					--unix \
					--absolute \
					$argv0]
				set gd [exec cygpath \
					--unix \
					--absolute \
					[gitdir]]
				set gw [exec cygpath \
					--windows \
					--absolute \
					[file dirname [gitdir]]]
				regsub -all ' $me "'\\''" me
				regsub -all ' $gd "'\\''" gd
				puts $fd "@ECHO Entering $gw"
				puts $fd "@ECHO Starting git-gui... please wait..."
				puts -nonewline $fd "@\"$sh\" --login -c \""
				puts -nonewline $fd "GIT_DIR='$gd'"
				puts -nonewline $fd " '$me'"
				puts $fd "&\""
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
				set gd [file normalize [gitdir]]
				set ep [file normalize [gitexec]]
				regsub -all ' $gd "'\\''" gd
				regsub -all ' $ep "'\\''" ep
				puts $fd "#!/bin/sh"
				foreach name [array names env] {
					if {[string match GIT_* $name]} {
						regsub -all ' $env($name) "'\\''" v
						puts $fd "export $name='$v'"
					}
				}
				puts $fd "export PATH='$ep':\$PATH"
				puts $fd "export GIT_DIR='$gd'"
				puts $fd "exec [file normalize $argv0]"
				close $fd

				file attributes $exe -permissions u+x,g+x,o+x
			} err]} {
			error_popup "Cannot write icon:\n\n$err"
		}
	}
}
