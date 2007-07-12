# git-gui console support
# Copyright (C) 2006, 2007 Shawn Pearce

class console {

field t_short
field t_long
field w
field console_cr

constructor new {short_title long_title} {
	set t_short $short_title
	set t_long $long_title
	_init $this
	return $this
}

method _init {} {
	global M1B
	make_toplevel top w -autodelete 0
	wm title $top "[appname] ([reponame]): $t_short"
	set console_cr 1.0

	frame $w.m
	label $w.m.l1 \
		-textvariable @t_long  \
		-anchor w \
		-justify left \
		-font font_uibold
	text $w.m.t \
		-background white -borderwidth 1 \
		-relief sunken \
		-width 80 -height 10 \
		-wrap none \
		-font font_diff \
		-state disabled \
		-xscrollcommand [list $w.m.sbx set] \
		-yscrollcommand [list $w.m.sby set]
	label $w.m.s -text {Working... please wait...} \
		-anchor w \
		-justify left \
		-font font_uibold
	scrollbar $w.m.sbx -command [list $w.m.t xview] -orient h
	scrollbar $w.m.sby -command [list $w.m.t yview]
	pack $w.m.l1 -side top -fill x
	pack $w.m.s -side bottom -fill x
	pack $w.m.sbx -side bottom -fill x
	pack $w.m.sby -side right -fill y
	pack $w.m.t -side left -fill both -expand 1
	pack $w.m -side top -fill both -expand 1 -padx 5 -pady 10

	menu $w.ctxm -tearoff 0
	$w.ctxm add command -label "Copy" \
		-command "tk_textCopy $w.m.t"
	$w.ctxm add command -label "Select All" \
		-command "focus $w.m.t;$w.m.t tag add sel 0.0 end"
	$w.ctxm add command -label "Copy All" \
		-command "
			$w.m.t tag add sel 0.0 end
			tk_textCopy $w.m.t
			$w.m.t tag remove sel 0.0 end
		"

	button $w.ok -text {Close} \
		-state disabled \
		-command "destroy $w"
	pack $w.ok -side bottom -anchor e -pady 10 -padx 10

	bind_button3 $w.m.t "tk_popup $w.ctxm %X %Y"
	bind $w.m.t <$M1B-Key-a> "$w.m.t tag add sel 0.0 end;break"
	bind $w.m.t <$M1B-Key-A> "$w.m.t tag add sel 0.0 end;break"
	bind $w <Visibility> "focus $w"
}

method exec {cmd {after {}}} {
	# -- Cygwin's Tcl tosses the enviroment when we exec our child.
	#    But most users need that so we have to relogin. :-(
	#
	if {[is_Cygwin]} {
		set cmd [list sh --login -c "cd \"[pwd]\" && [join $cmd { }]"]
	}

	# -- Tcl won't let us redirect both stdout and stderr to
	#    the same pipe.  So pass it through cat...
	#
	set cmd [concat | $cmd |& cat]

	set fd_f [open $cmd r]
	fconfigure $fd_f -blocking 0 -translation binary
	fileevent $fd_f readable [cb _read $fd_f $after]
}

method _read {fd after} {
	set buf [read $fd]
	if {$buf ne {}} {
		if {![winfo exists $w.m.t]} {_init $this}
		$w.m.t conf -state normal
		set c 0
		set n [string length $buf]
		while {$c < $n} {
			set cr [string first "\r" $buf $c]
			set lf [string first "\n" $buf $c]
			if {$cr < 0} {set cr [expr {$n + 1}]}
			if {$lf < 0} {set lf [expr {$n + 1}]}

			if {$lf < $cr} {
				$w.m.t insert end [string range $buf $c $lf]
				set console_cr [$w.m.t index {end -1c}]
				set c $lf
				incr c
			} else {
				$w.m.t delete $console_cr end
				$w.m.t insert end "\n"
				$w.m.t insert end [string range $buf $c $cr]
				set c $cr
				incr c
			}
		}
		$w.m.t conf -state disabled
		$w.m.t see end
	}

	fconfigure $fd -blocking 1
	if {[eof $fd]} {
		if {[catch {close $fd}]} {
			set ok 0
		} else {
			set ok 1
		}
		if {$after ne {}} {
			uplevel #0 $after $ok
		} else {
			done $this $ok
		}
		return
	}
	fconfigure $fd -blocking 0
}

method chain {cmdlist {ok 1}} {
	if {$ok} {
		if {[llength $cmdlist] == 0} {
			done $this $ok
			return
		}

		set cmd [lindex $cmdlist 0]
		set cmdlist [lrange $cmdlist 1 end]

		if {[lindex $cmd 0] eq {exec}} {
			exec $this \
				[lrange $cmd 1 end] \
				[cb chain $cmdlist]
		} else {
			uplevel #0 $cmd [cb chain $cmdlist]
		}
	} else {
		done $this $ok
	}
}

method done {ok} {
	if {$ok} {
		if {[winfo exists $w.m.s]} {
			$w.m.s conf -background green -text {Success}
			$w.ok conf -state normal
			focus $w.ok
		}
	} else {
		if {![winfo exists $w.m.s]} {
			_init $this
		}
		$w.m.s conf -background red -text {Error: Command Failed}
		$w.ok conf -state normal
		focus $w.ok
	}
	delete_this
}

}
