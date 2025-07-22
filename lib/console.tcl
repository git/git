# git-gui console support
# Copyright (C) 2006, 2007 Shawn Pearce

class console {

field t_short
field t_long
field w
field w_t
field console_cr
field is_toplevel    1; # are we our own window?

constructor new {short_title long_title} {
	set t_short $short_title
	set t_long $long_title
	_init $this
	return $this
}

constructor embed {path title} {
	set t_short {}
	set t_long $title
	set w $path
	set is_toplevel 0
	_init $this
	return $this
}

method _init {} {
	global M1B

	if {$is_toplevel} {
		make_dialog top w -autodelete 0
		wm title $top "[appname] ([reponame]): $t_short"
	} else {
		ttk::frame $w
	}

	set console_cr 1.0
	set w_t $w.m.t

	ttk::frame $w.m
	ttk::label $w.m.l1 \
		-textvariable @t_long  \
		-anchor w \
		-justify left \
		-font font_uibold
	text $w_t \
		-background white \
		-foreground black \
		-borderwidth 1 \
		-relief sunken \
		-width 80 -height 10 \
		-wrap none \
		-font font_diff \
		-state disabled \
		-xscrollcommand [cb _sb_set $w.m.sbx h] \
		-yscrollcommand [cb _sb_set $w.m.sby v]
	label $w.m.s -text [mc "Working... please wait..."] \
		-anchor w \
		-justify left \
		-font font_uibold
	pack $w.m.l1 -side top -fill x
	pack $w.m.s -side bottom -fill x
	pack $w_t -side left -fill both -expand 1
	pack $w.m -side top -fill both -expand 1 -padx 5 -pady 10

	menu $w.ctxm -tearoff 0
	$w.ctxm add command -label [mc "Copy"] \
		-command "tk_textCopy $w_t"
	$w.ctxm add command -label [mc "Select All"] \
		-command "focus $w_t;$w_t tag add sel 0.0 end"
	$w.ctxm add command -label [mc "Copy All"] \
		-command "
			$w_t tag add sel 0.0 end
			tk_textCopy $w_t
			$w_t tag remove sel 0.0 end
		"

	if {$is_toplevel} {
		ttk::button $w.ok -text [mc "Close"] \
			-state disabled \
			-command [list destroy $w]
		pack $w.ok -side bottom -anchor e -pady 10 -padx 10
		bind $w <Visibility> [list focus $w]
	}

	bind_button3 $w_t "tk_popup $w.ctxm %X %Y"
	bind $w_t <$M1B-Key-a> "$w_t tag add sel 0.0 end;break"
	bind $w_t <$M1B-Key-A> "$w_t tag add sel 0.0 end;break"
}

method exec {cmd {after {}}} {
	if {[lindex $cmd 0] eq {git}} {
		set fd_f [git_read [lrange $cmd 1 end] [list 2>@1]]
	} else {
		set fd_f [safe_open_command $cmd [list 2>@1]]
	}
	fconfigure $fd_f -blocking 0 -translation binary -encoding [encoding system]
	fileevent $fd_f readable [cb _read $fd_f $after]
}

method _read {fd after} {
	set buf [read $fd]
	if {$buf ne {}} {
		if {![winfo exists $w_t]} {_init $this}
		$w_t conf -state normal
		set c 0
		set n [string length $buf]
		while {$c < $n} {
			set cr [string first "\r" $buf $c]
			set lf [string first "\n" $buf $c]
			if {$cr < 0} {set cr [expr {$n + 1}]}
			if {$lf < 0} {set lf [expr {$n + 1}]}

			if {$lf < $cr} {
				$w_t insert end [string range $buf $c $lf]
				set console_cr [$w_t index {end -1c}]
				set c $lf
				incr c
			} else {
				$w_t delete $console_cr end
				$w_t insert end "\n"
				$w_t insert end [string range $buf $c [expr {$cr - 1}]]
				set c $cr
				incr c
			}
		}
		$w_t conf -state disabled
		$w_t see end
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

method insert {txt} {
	if {![winfo exists $w_t]} {_init $this}
	$w_t conf -state normal
	$w_t insert end "$txt\n"
	set console_cr [$w_t index {end -1c}]
	$w_t conf -state disabled
}

method done {ok} {
	if {$ok} {
		if {[winfo exists $w.m.s]} {
			bind $w.m.s <Destroy> [list delete_this $this]
			$w.m.s conf -background green -foreground black \
				-text [mc "Success"]
			if {$is_toplevel} {
				$w.ok conf -state normal
				focus $w.ok
			}
		} else {
			delete_this
		}
	} else {
		if {![winfo exists $w.m.s]} {
			_init $this
		}
		bind $w.m.s <Destroy> [list delete_this $this]
		$w.m.s conf -background red -foreground black \
			-text [mc "Error: Command Failed"]
		if {$is_toplevel} {
			$w.ok conf -state normal
			focus $w.ok
		}
	}

	bind $w <Key-Escape> "destroy $w;break"
}

method _sb_set {sb orient first last} {
	if {![winfo exists $sb]} {
		if {$first == $last || ($first == 0 && $last == 1)} return
		if {$orient eq {h}} {
			ttk::scrollbar $sb -orient h -command [list $w_t xview]
			pack $sb -fill x -side bottom -before $w_t
		} else {
			ttk::scrollbar $sb -orient v -command [list $w_t yview]
			pack $sb -fill y -side right -before $w_t
		}
	}
	$sb set $first $last
}

}
