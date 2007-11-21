// git-gui Windows shortcut support
// Copyright (C) 2007 Shawn Pearce

var WshShell = WScript.CreateObject("WScript.Shell");
var argv = WScript.Arguments;
var argi = 0;
var lnk_path = argv.item(argi++);
var ico_path = argi < argv.length ? argv.item(argi++) : undefined;
var dir_path = argi < argv.length ? argv.item(argi++) : undefined;
var lnk_exec = argi < argv.length ? argv.item(argi++) : undefined;
var lnk_args = '';
while (argi < argv.length) {
	var s = argv.item(argi++);
	if (lnk_args != '')
		lnk_args += ' ';
	if (s.indexOf(' ') >= 0) {
		lnk_args += '"';
		lnk_args += s;
		lnk_args += '"';
	} else {
		lnk_args += s;
	}
}

var lnk = WshShell.CreateShortcut(lnk_path);
if (argv.length == 1) {
	WScript.echo(lnk.TargetPath);
} else {
	lnk.TargetPath = lnk_exec;
	lnk.Arguments = lnk_args;
	lnk.IconLocation = ico_path + ", 0";
	lnk.WorkingDirectory = dir_path;
	lnk.Save();
}
