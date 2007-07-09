# git-gui object database management support
# Copyright (C) 2006, 2007 Shawn Pearce

proc do_stats {} {
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
	toplevel $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	label $w.header -text {Database Statistics}
	pack $w.header -side top -fill x

	frame $w.buttons -border 1
	button $w.buttons.close -text Close \
		-default active \
		-command [list destroy $w]
	button $w.buttons.gc -text {Compress Database} \
		-default normal \
		-command "destroy $w;do_gc"
	pack $w.buttons.close -side right
	pack $w.buttons.gc -side left
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	frame $w.stat -borderwidth 1 -relief solid
	foreach s {
		{count           {Number of loose objects}}
		{size            {Disk space used by loose objects} { KiB}}
		{in-pack         {Number of packed objects}}
		{packs           {Number of packs}}
		{size-pack       {Disk space used by packed objects} { KiB}}
		{prune-packable  {Packed objects waiting for pruning}}
		{garbage         {Garbage files}}
		} {
		set name [lindex $s 0]
		set label [lindex $s 1]
		if {[catch {set value $stats($name)}]} continue
		if {[llength $s] > 2} {
			set value "$value[lindex $s 2]"
		}

		label $w.stat.l_$name -text "$label:" -anchor w
		label $w.stat.v_$name -text $value -anchor w
		grid $w.stat.l_$name $w.stat.v_$name -sticky we -padx {0 5}
	}
	pack $w.stat -pady 10 -padx 10

	bind $w <Visibility> "grab $w; focus $w.buttons.close"
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Key-Return> [list destroy $w]
	wm title $w "[appname] ([reponame]): Database Statistics"
	tkwait window $w
}

proc do_gc {} {
	set w [console::new {gc} {Compressing the object database}]
	console::chain $w {
		{exec git pack-refs --prune}
		{exec git reflog expire --all}
		{exec git repack -a -d -l}
		{exec git rerere gc}
	}
}

proc do_fsck_objects {} {
	set w [console::new {fsck-objects} \
		{Verifying the object database with fsck-objects}]
	set cmd [list git fsck-objects]
	lappend cmd --full
	lappend cmd --cache
	lappend cmd --strict
	console::exec $w $cmd
}
