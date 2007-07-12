# git-gui revision chooser
# Copyright (C) 2006, 2007 Shawn Pearce

class choose_rev {

image create photo ::choose_rev::img_find -data {R0lGODlhEAAQAIYAAPwCBCQmJDw+PBQSFAQCBMza3NTm5MTW1HyChOT29Ozq7MTq7Kze5Kzm7Oz6/NTy9Iza5GzGzKzS1Nzy9Nz29Kzq9HTGzHTK1Lza3AwKDLzu9JTi7HTW5GTCzITO1Mzq7Hza5FTK1ESyvHzKzKzW3DQyNDyqtDw6PIzW5HzGzAT+/Dw+RKyurNTOzMTGxMS+tJSGdATCxHRydLSqpLymnLSijBweHERCRNze3Pz69PTy9Oze1OTSxOTGrMSqlLy+vPTu5OzSvMymjNTGvNS+tMy2pMyunMSefAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAALAAAAAAQABAAAAe4gACCAAECA4OIiAIEBQYHBAKJgwIICQoLDA0IkZIECQ4PCxARCwSSAxITFA8VEBYXGBmJAQYLGhUbHB0eH7KIGRIMEBAgISIjJKaIJQQLFxERIialkieUGigpKRoIBCqJKyyLBwvJAioEyoICLS4v6QQwMQQyLuqLli8zNDU2BCf1lN3AkUPHDh49fAQAAEnGD1MCCALZEaSHkIUMBQS8wWMIkSJGhBzBmFEGgRsBUqpMiSgdAD+BAAAh/mhDcmVhdGVkIGJ5IEJNUFRvR0lGIFBybyB2ZXJzaW9uIDIuNQ0KqSBEZXZlbENvciAxOTk3LDE5OTguIEFsbCByaWdodHMgcmVzZXJ2ZWQuDQpodHRwOi8vd3d3LmRldmVsY29yLmNvbQA7}

field w               ; # our megawidget path
field w_list          ; # list of currently filtered specs
field w_filter        ; # filter entry for $w_list

field c_expr        {}; # current revision expression
field filter          ; # current filter string
field revtype     head; # type of revision chosen
field cur_specs [list]; # list of specs for $revtype
field spec_head       ; # list of all head specs
field spec_trck       ; # list of all tracking branch specs
field spec_tag        ; # list of all tag specs

constructor new {path {title {}}} {
	global current_branch is_detached

	set w $path

	if {$title ne {}} {
		labelframe $w -text $title
	} else {
		frame $w
	}
	bind $w <Destroy> [cb _delete %W]

	if {$is_detached} {
		radiobutton $w.detachedhead_r \
			-anchor w \
			-text {This Detached Checkout} \
			-value HEAD \
			-variable @revtype
		grid $w.detachedhead_r -sticky we -padx {0 5} -columnspan 2
	}

	radiobutton $w.expr_r \
		-text {Revision Expression:} \
		-value expr \
		-variable @revtype
	entry $w.expr_t \
		-borderwidth 1 \
		-relief sunken \
		-width 50 \
		-textvariable @c_expr \
		-validate key \
		-validatecommand [cb _validate %d %S]
	grid $w.expr_r $w.expr_t -sticky we -padx {0 5}

	frame $w.types
	radiobutton $w.types.head_r \
		-text {Local Branch} \
		-value head \
		-variable @revtype
	pack $w.types.head_r -side left
	radiobutton $w.types.trck_r \
		-text {Tracking Branch} \
		-value trck \
		-variable @revtype
	pack $w.types.trck_r -side left
	radiobutton $w.types.tag_r \
		-text {Tag} \
		-value tag \
		-variable @revtype
	pack $w.types.tag_r -side left
	set w_filter $w.types.filter
	entry $w_filter \
		-borderwidth 1 \
		-relief sunken \
		-width 12 \
		-textvariable @filter \
		-validate key \
		-validatecommand [cb _filter %P]
	pack $w_filter -side right
	pack [label $w.types.filter_icon \
		-image ::choose_rev::img_find \
		] -side right
	grid $w.types -sticky we -padx {0 5} -columnspan 2

	frame $w.list
	set w_list $w.list.l
	listbox $w_list \
		-font font_diff \
		-width 50 \
		-height 5 \
		-selectmode browse \
		-exportselection false \
		-xscrollcommand [cb _sb_set $w.list.sbx h] \
		-yscrollcommand [cb _sb_set $w.list.sby v]
	pack $w_list -fill both -expand 1
	grid $w.list -sticky nswe -padx {20 5} -columnspan 2

	grid columnconfigure $w 1 -weight 1
	if {$is_detached} {
		grid rowconfigure $w 3 -weight 1
	} else {
		grid rowconfigure $w 2 -weight 1
	}

	trace add variable @revtype write [cb _select]
	bind $w_filter <Key-Return> [list focus $w_list]\;break
	bind $w_filter <Key-Down>   [list focus $w_list]

	set spec_head [list]
	foreach name [load_all_heads] {
		lappend spec_head [list $name refs/heads/$name]
	}

	set spec_trck [list]
	foreach spec [all_tracking_branches] {
		set name [lindex $spec 0]
		regsub ^refs/(heads|remotes)/ $name {} name
		lappend spec_trck [concat $name $spec]
	}

	set spec_tag [list]
	foreach name [load_all_tags] {
		lappend spec_tag [list $name refs/tags/$name]
	}

		  if {$is_detached}             { set revtype HEAD
	} elseif {[llength $spec_head] > 0} { set revtype head
	} elseif {[llength $spec_trck] > 0} { set revtype trck
	} elseif {[llength $spec_tag ] > 0} { set revtype tag
	} else {                              set revtype expr
	}

	if {$revtype eq {head} && $current_branch ne {}} {
		set i 0
		foreach spec $spec_head {
			if {[lindex $spec 0] eq $current_branch} {
				$w_list selection clear 0 end
				$w_list selection set $i
				break
			}
			incr i
		}
	}

	return $this
}

method none {text} {
	if {![winfo exists $w.none_r]} {
		radiobutton $w.none_r \
			-anchor w \
			-value none \
			-variable @revtype
		grid $w.none_r -sticky we -padx {0 5} -columnspan 2
	}
	$w.none_r configure -text $text
}

method get {} {
	switch -- $revtype {
	head -
	trck -
	tag  {
		set i [$w_list curselection]
		if {$i ne {}} {
			return [lindex $cur_specs $i 0]
		} else {
			return {}
		}
	}

	HEAD { return HEAD                     }
	expr { return $c_expr                  }
	none { return {}                       }
	default { error "unknown type of revision" }
	}
}

method pick_tracking_branch {} {
	set revtype trck
}

method focus_filter {} {
	if {[$w_filter cget -state] eq {normal}} {
		focus $w_filter
	}
}

method bind_listbox {event script}  {
	bind $w_list $event $script
}

method get_local_branch {} {
	if {$revtype eq {head}} {
		return [_expr $this]
	} else {
		return {}
	}
}

method get_tracking_branch {} {
	set i [$w_list curselection]
	if {$i eq {} || $revtype ne {trck}} {
		return {}
	}
	return [lrange [lindex $cur_specs $i] 1 end]
}

method get_commit {} {
	set e [_expr $this]
	if {$e eq {}} {
		return {}
	}
	return [git rev-parse --verify "$e^0"]
}

method commit_or_die {} {
	if {[catch {set new [get_commit $this]} err]} {

		# Cleanup the not-so-friendly error from rev-parse.
		#
		regsub {^fatal:\s*} $err {} err
		if {$err eq {Needed a single revision}} {
			set err {}
		}

		set top [winfo toplevel $w]
		set msg "Invalid revision: [get $this]\n\n$err"
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $top] \
			-parent $top \
			-message $msg
		error $msg
	}
	return $new
}

method _expr {} {
	switch -- $revtype {
	head -
	trck -
	tag  {
		set i [$w_list curselection]
		if {$i ne {}} {
			return [lindex $cur_specs $i 1]
		} else {
			error "No revision selected."
		}
	}

	expr {
		if {$c_expr ne {}} {
			return $c_expr
		} else {
			error "Revision expression is empty."
		}
	}
	HEAD { return HEAD                     }
	none { return {}                       }
	default { error "unknown type of revision"      }
	}
}

method _validate {d S} {
	if {$d == 1} {
		if {[regexp {\s} $S]} {
			return 0
		}
		if {[string length $S] > 0} {
			set revtype expr
		}
	}
	return 1
}

method _filter {P} {
	if {[regexp {\s} $P]} {
		return 0
	}
	_rebuild $this $P
	return 1
}

method _select {args} {
	_rebuild $this $filter
	focus_filter $this
}

method _rebuild {pat} {
	set ste normal
	switch -- $revtype {
	head { set new $spec_head }
	trck { set new $spec_trck }
	tag  { set new $spec_tag  }
	expr -
	HEAD -
	none {
		set new [list]
		set ste disabled
	}
	}

	if {[$w_list cget -state] eq {disabled}} {
		$w_list configure -state normal
	}
	$w_list delete 0 end

	if {$pat ne {}} {
		set pat *${pat}*
	}
	set cur_specs [list]
	foreach spec $new {
		set txt [lindex $spec 0]
		if {$pat eq {} || [string match $pat $txt]} {
			lappend cur_specs $spec
			$w_list insert end $txt
		}
	}
	if {$cur_specs ne {}} {
		$w_list selection clear 0 end
		$w_list selection set 0
	}

	if {[$w_filter cget -state] ne $ste} {
		$w_list   configure -state $ste
		$w_filter configure -state $ste
	}
}

method _delete {current} {
	if {$current eq $w} {
		delete_this
	}
}

method _sb_set {sb orient first last} {
	set old_focus [focus -lastfor $w]

	if {$first == 0 && $last == 1} {
		if {[winfo exists $sb]} {
			destroy $sb
			if {$old_focus ne {}} {
				update
				focus $old_focus
			}
		}
		return
	}

	if {![winfo exists $sb]} {
		if {$orient eq {h}} {
			scrollbar $sb -orient h -command [list $w_list xview]
			pack $sb -fill x -side bottom -before $w_list
		} else {
			scrollbar $sb -orient v -command [list $w_list yview]
			pack $sb -fill y -side right -before $w_list
		}
		if {$old_focus ne {}} {
			update
			focus $old_focus
		}
	}
	$sb set $first $last
}

}
