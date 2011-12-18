# git-gui object database management support
# Copyright (C) 2006, 2007 Shawn Pearce

proc do_stats {} {
	global use_ttk NS
	set fd [git_read count-objects -v]
	while {[gets $fd line] > 0} {
		if {[regexp {^([^:]+): (\d+)$} $line _ name value]} {
			set stats($name) $value
		}
	}
	close $fd

	set packed_sz 0
	foreach p [glob -directory [gitdir objects pack] \
		-type f \
		-nocomplain -- *] {
		incr packed_sz [file size $p]
	}
	if {$packed_sz > 0} {
		set stats(size-pack) [expr {$packed_sz / 1024}]
	}

	set w .stats_view
	Dialog $w
	wm withdraw $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	${NS}::frame $w.buttons
	${NS}::button $w.buttons.close -text [mc Close] \
		-default active \
		-command [list destroy $w]
	${NS}::button $w.buttons.gc -text [mc "Compress Database"] \
		-default normal \
		-command "destroy $w;do_gc"
	pack $w.buttons.close -side right
	pack $w.buttons.gc -side left
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	${NS}::labelframe $w.stat -text [mc "Database Statistics"]
	foreach s {
		{count           {mc "Number of loose objects"}}
		{size            {mc "Disk space used by loose objects"} { KiB}}
		{in-pack         {mc "Number of packed objects"}}
		{packs           {mc "Number of packs"}}
		{size-pack       {mc "Disk space used by packed objects"} { KiB}}
		{prune-packable  {mc "Packed objects waiting for pruning"}}
		{garbage         {mc "Garbage files"}}
		} {
		set name [lindex $s 0]
		set label [eval [lindex $s 1]]
		if {[catch {set value $stats($name)}]} continue
		if {[llength $s] > 2} {
			set value "$value[lindex $s 2]"
		}

		${NS}::label $w.stat.l_$name -text "$label:" -anchor w
		${NS}::label $w.stat.v_$name -text $value -anchor w
		grid $w.stat.l_$name $w.stat.v_$name -sticky we -padx {0 5}
	}
	pack $w.stat -pady 10 -padx 10

	bind $w <Visibility> "grab $w; focus $w.buttons.close"
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Key-Return> [list destroy $w]
	wm title $w [append "[appname] ([reponame]): " [mc "Database Statistics"]]
	wm deiconify $w
	tkwait window $w
}

proc do_gc {} {
	set w [console::new {gc} [mc "Compressing the object database"]]
	console::chain $w {
		{exec git pack-refs --prune}
		{exec git reflog expire --all}
		{exec git repack -a -d -l}
		{exec git rerere gc}
	}
}

proc do_fsck_objects {} {
	set w [console::new {fsck-objects} \
		[mc "Verifying the object database with fsck-objects"]]
	set cmd [list git fsck-objects]
	lappend cmd --full
	lappend cmd --cache
	lappend cmd --strict
	console::exec $w $cmd
}

proc hint_gc {} {
	set ndirs 1
	set limit 8
	if {[is_Windows]} {
		set ndirs 4
		set limit 1
	}

	set count [llength [glob \
		-nocomplain \
		-- \
		[gitdir objects 4\[0-[expr {$ndirs-1}]\]/*]]]

	if {$count >= $limit * $ndirs} {
		set objects_current [expr {$count * 256/$ndirs}]
		if {[ask_popup \
			[mc "This repository currently has approximately %i loose objects.

To maintain optimal performance it is strongly recommended that you compress the database.

Compress the database now?" $objects_current]] eq yes} {
			do_gc
		}
	}
}
