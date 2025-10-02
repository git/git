# git-gui branch (create/delete) support
# Copyright (C) 2006, 2007 Shawn Pearce

proc load_all_heads {} {
	global some_heads_tracking

	set rh refs/heads
	set rh_len [expr {[string length $rh] + 1}]
	set all_heads [list]
	set fd [git_read [list for-each-ref --format=%(refname) $rh]]
	fconfigure $fd -encoding utf-8
	while {[gets $fd line] > 0} {
		if {!$some_heads_tracking || ![is_tracking_branch $line]} {
			lappend all_heads [string range $line $rh_len end]
		}
	}
	close $fd

	return [lsort $all_heads]
}

proc load_all_tags {} {
	set all_tags [list]
	set fd [git_read [list for-each-ref \
		--sort=-taggerdate \
		--format=%(refname) \
		refs/tags]]
	fconfigure $fd -encoding utf-8
	while {[gets $fd line] > 0} {
		if {![regsub ^refs/tags/ $line {} name]} continue
		lappend all_tags $name
	}
	close $fd
	return $all_tags
}

proc radio_selector {varname value args} {
	upvar #0 $varname var
	set var $value
}
