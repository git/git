# git-gui desktop icon creators
# Copyright (C) 2006, 2007 Shawn Pearce

proc do_windows_shortcut {} {
	global _gitworktree
	set fn [tk_getSaveFile \
		-parent . \
		-title [mc "%s (%s): Create Desktop Icon" [appname] [reponame]] \
		-initialfile "Git [reponame].lnk"]
	if {$fn != {}} {
		if {[file extension $fn] ne {.lnk}} {
			set fn ${fn}.lnk
		}
		# Use git-gui.exe if available (ie: git-for-windows)
		set cmdLine [list [_which git-gui]]
		if {$cmdLine eq {}} {
			set cmdLine [list [info nameofexecutable] \
							 [file normalize $::argv0]]
		}
		if {[catch {
				win32_create_lnk $fn $cmdLine \
					[file normalize $_gitworktree]
			} err]} {
			error_popup [strcat [mc "Cannot write shortcut:"] "\n\n$err"]
		}
	}
}

proc do_cygwin_shortcut {} {
	global argv0 _gitworktree oguilib

	if {[catch {
		set desktop [safe_exec [list cygpath \
			--desktop]]
		}]} {
			set desktop .
	}
	set fn [tk_getSaveFile \
		-parent . \
		-title [mc "%s (%s): Create Desktop Icon" [appname] [reponame]] \
		-initialdir $desktop \
		-initialfile "Git [reponame].lnk"]
	if {$fn != {}} {
		if {[file extension $fn] ne {.lnk}} {
			set fn ${fn}.lnk
		}
		if {[catch {
				set repodir [file normalize $_gitworktree]
				set shargs {-c \
					"CHERE_INVOKING=1 \
					source /etc/profile; \
					git gui"}
				safe_exec [list /bin/mkshortcut.exe \
					--arguments $shargs \
					--desc "git-gui on $repodir" \
					--icon $oguilib/git-gui.ico \
					--name $fn \
					--show min \
					--workingdir $repodir \
					/bin/sh.exe]
			} err]} {
			error_popup [strcat [mc "Cannot write shortcut:"] "\n\n$err"]
		}
	}
}

proc do_macosx_app {} {
	global argv0 env

	set fn [tk_getSaveFile \
		-parent . \
		-title [mc "%s (%s): Create Desktop Icon" [appname] [reponame]] \
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

				set fd [safe_open_file [file join $Contents Info.plist] w]
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

				set fd [safe_open_file $exe w]
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
			error_popup [strcat [mc "Cannot write icon:"] "\n\n$err"]
		}
	}
}
