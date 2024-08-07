# git-gui Misc. native Windows 32 support
# Copyright (C) 2007 Shawn Pearce

proc win32_read_lnk {lnk_path} {
	return [exec cscript.exe \
		/E:jscript \
		/nologo \
		[file join $::oguilib win32_shortcut.js] \
		$lnk_path]
}

proc win32_create_lnk {lnk_path lnk_exec lnk_dir} {
	global oguilib

	set lnk_args [lrange $lnk_exec 1 end]
	set lnk_exec [lindex $lnk_exec 0]

	eval [list exec wscript.exe \
		/E:jscript \
		/nologo \
		[file nativename [file join $oguilib win32_shortcut.js]] \
		$lnk_path \
		[file nativename [file join $oguilib git-gui.ico]] \
		$lnk_dir \
		$lnk_exec] $lnk_args
}
