# git-gui Tools menu implementation

proc tools_list {} {
	global repo_config

	set names {}
	foreach item [array names repo_config guitool.*.cmd] {
		lappend names [string range $item 8 end-4]
	}
	return [lsort $names]
}

proc tools_populate_all {} {
	global tools_menubar tools_menutbl
	global tools_tailcnt

	set mbar_end [$tools_menubar index end]
	set mbar_base [expr {$mbar_end - $tools_tailcnt}]
	if {$mbar_base >= 0} {
		$tools_menubar delete 0 $mbar_base
	}

	array unset tools_menutbl

	foreach fullname [tools_list] {
		tools_populate_one $fullname
	}
}

proc tools_create_item {parent args} {
	global tools_menubar tools_tailcnt
	if {$parent eq $tools_menubar} {
		set pos [expr {[$parent index end]-$tools_tailcnt+1}]
		eval [list $parent insert $pos] $args
	} else {
		eval [list $parent add] $args
	}
}

proc tools_populate_one {fullname} {
	global tools_menubar tools_menutbl tools_id

	if {![info exists tools_id]} {
		set tools_id 0
	}

	set names [split $fullname '/']
	set parent $tools_menubar
	for {set i 0} {$i < [llength $names]-1} {incr i} {
		set subname [join [lrange $names 0 $i] '/']
		if {[info exists tools_menutbl($subname)]} {
			set parent $tools_menutbl($subname)
		} else {
			set subid $parent.t$tools_id
			tools_create_item $parent cascade \
					-label [lindex $names $i] -menu $subid
			menu $subid
			set tools_menutbl($subname) $subid
			set parent $subid
			incr tools_id
		}
	}

	tools_create_item $parent command \
		-label [lindex $names end] \
		-command [list tools_exec $fullname]
}

proc tools_exec {fullname} {
	global repo_config env current_diff_path
	global current_branch is_detached

	if {[is_config_true "guitool.$fullname.needsfile"]} {
		if {$current_diff_path eq {}} {
			error_popup [mc "Running %s requires a selected file." $fullname]
			return
		}
	}

	if {[is_config_true "guitool.$fullname.confirm"]} {
		if {[ask_popup [mc "Are you sure you want to run %s?" $fullname]] ne {yes}} {
			return
		}
	}

	set env(GIT_GUITOOL) $fullname
	set env(FILENAME) $current_diff_path
	if {$is_detached} {
		set env(CUR_BRANCH) ""
	} else {
		set env(CUR_BRANCH) $current_branch
	}

	set cmdline $repo_config(guitool.$fullname.cmd)
	if {[is_config_true "guitool.$fullname.noconsole"]} {
		exec sh -c $cmdline &
	} else {
		regsub {/} $fullname { / } title
		set w [console::new \
			[mc "Tool: %s" $title] \
			[mc "Running: %s" $cmdline]]
		console::exec $w [list sh -c $cmdline]
	}

	unset env(GIT_GUITOOL)
	unset env(FILENAME)
	unset env(CUR_BRANCH)
}
