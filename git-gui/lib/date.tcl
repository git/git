# git-gui date processing support
# Copyright (C) 2007 Shawn Pearce

set git_month(Jan)  1
set git_month(Feb)  2
set git_month(Mar)  3
set git_month(Apr)  4
set git_month(May)  5
set git_month(Jun)  6
set git_month(Jul)  7
set git_month(Aug)  8
set git_month(Sep)  9
set git_month(Oct) 10
set git_month(Nov) 11
set git_month(Dec) 12

proc parse_git_date {s} {
	if {$s eq {}} {
		return {}
	}

	if {![regexp \
		{^... (...) (\d{1,2}) (\d\d):(\d\d):(\d\d) (\d{4}) ([+-]?)(\d\d)(\d\d)$} $s s \
		month day hr mm ss yr ew tz_h tz_m]} {
		error [mc "Invalid date from Git: %s" $s]
	}

	set s [clock scan [format {%4.4i%2.2i%2.2iT%2s%2s%2s} \
			$yr $::git_month($month) $day \
			$hr $mm $ss] \
			-gmt 1]

	regsub ^0 $tz_h {} tz_h
	regsub ^0 $tz_m {} tz_m
	switch -- $ew {
	-  {set ew +}
	+  {set ew -}
	{} {set ew -}
	}

	return [expr "$s $ew ($tz_h * 3600 + $tz_m * 60)"]
}

proc format_date {s} {
	if {$s eq {}} {
		return {}
	}
	return [clock format $s -format {%a %b %e %H:%M:%S %Y}]
}

proc reformat_date {s} {
	return [format_date [parse_git_date $s]]
}
