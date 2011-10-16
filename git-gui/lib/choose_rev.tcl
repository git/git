# git-gui revision chooser
# Copyright (C) 2006, 2007 Shawn Pearce

class choose_rev {

image create photo ::choose_rev::img_find -data {R0lGODlhEAAQAIYAAPwCBCQmJDw+PBQSFAQCBMza3NTm5MTW1HyChOT29Ozq7MTq7Kze5Kzm7Oz6/NTy9Iza5GzGzKzS1Nzy9Nz29Kzq9HTGzHTK1Lza3AwKDLzu9JTi7HTW5GTCzITO1Mzq7Hza5FTK1ESyvHzKzKzW3DQyNDyqtDw6PIzW5HzGzAT+/Dw+RKyurNTOzMTGxMS+tJSGdATCxHRydLSqpLymnLSijBweHERCRNze3Pz69PTy9Oze1OTSxOTGrMSqlLy+vPTu5OzSvMymjNTGvNS+tMy2pMyunMSefAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAALAAAAAAQABAAAAe4gACCAAECA4OIiAIEBQYHBAKJgwIICQoLDA0IkZIECQ4PCxARCwSSAxITFA8VEBYXGBmJAQYLGhUbHB0eH7KIGRIMEBAgISIjJKaIJQQLFxERIialkieUGigpKRoIBCqJKyyLBwvJAioEyoICLS4v6QQwMQQyLuqLli8zNDU2BCf1lN3AkUPHDh49fAQAAEnGD1MCCALZEaSHkIUMBQS8wWMIkSJGhBzBmFEGgRsBUqpMiSgdAD+BAAAh/mhDcmVhdGVkIGJ5IEJNUFRvR0lGIFBybyB2ZXJzaW9uIDIuNQ0KqSBEZXZlbENvciAxOTk3LDE5OTguIEFsbCByaWdodHMgcmVzZXJ2ZWQuDQpodHRwOi8vd3d3LmRldmVsY29yLmNvbQA7}

field w               ; # our megawidget path
field w_list          ; # list of currently filtered specs
field w_filter        ; # filter entry for $w_list

field c_expr        {}; # current revision expression
field filter        ""; # current filter string
field revtype     head; # type of revision chosen
field cur_specs [list]; # list of specs for $revtype
field spec_head       ; # list of all head specs
field spec_trck       ; # list of all tracking branch specs
field spec_tag        ; # list of all tag specs
field tip_data        ; # array of tip commit info by refname
field log_last        ; # array of reflog date by refname

field tooltip_wm        {} ; # Current tooltip toplevel, if open
field tooltip_t         {} ; # Text widget in $tooltip_wm
field tooltip_timer     {} ; # Current timer event for our tooltip

proc new {path {title {}}} {
	return [_new $path 0 $title]
}

proc new_unmerged {path {title {}}} {
	return [_new $path 1 $title]
}

constructor _new {path unmerged_only title} {
	global current_branch is_detached use_ttk NS

	if {![info exists ::all_remotes]} {
		load_all_remotes
	}

	set w $path

	if {$title ne {}} {
		${NS}::labelframe $w -text $title
	} else {
		${NS}::frame $w
	}
	bind $w <Destroy> [cb _delete %W]

	if {$is_detached} {
		${NS}::radiobutton $w.detachedhead_r \
			-text [mc "This Detached Checkout"] \
			-value HEAD \
			-variable @revtype
		if {!$use_ttk} {$w.detachedhead_r configure -anchor w}
		grid $w.detachedhead_r -sticky we -padx {0 5} -columnspan 2
	}

	${NS}::radiobutton $w.expr_r \
		-text [mc "Revision Expression:"] \
		-value expr \
		-variable @revtype
	${NS}::entry $w.expr_t \
		-width 50 \
		-textvariable @c_expr \
		-validate key \
		-validatecommand [cb _validate %d %S]
	grid $w.expr_r $w.expr_t -sticky we -padx {0 5}

	${NS}::frame $w.types
	${NS}::radiobutton $w.types.head_r \
		-text [mc "Local Branch"] \
		-value head \
		-variable @revtype
	pack $w.types.head_r -side left
	${NS}::radiobutton $w.types.trck_r \
		-text [mc "Tracking Branch"] \
		-value trck \
		-variable @revtype
	pack $w.types.trck_r -side left
	${NS}::radiobutton $w.types.tag_r \
		-text [mc "Tag"] \
		-value tag \
		-variable @revtype
	pack $w.types.tag_r -side left
	set w_filter $w.types.filter
	${NS}::entry $w_filter \
		-width 12 \
		-textvariable @filter \
		-validate key \
		-validatecommand [cb _filter %P]
	pack $w_filter -side right
	pack [${NS}::label $w.types.filter_icon \
		-image ::choose_rev::img_find \
		] -side right
	grid $w.types -sticky we -padx {0 5} -columnspan 2

	if {$use_ttk} {
		ttk::frame $w.list -style SListbox.TFrame -padding 2
	} else {
		frame $w.list
	}
	set w_list $w.list.l
	listbox $w_list \
		-font font_diff \
		-width 50 \
		-height 10 \
		-selectmode browse \
		-exportselection false \
		-xscrollcommand [cb _sb_set $w.list.sbx h] \
		-yscrollcommand [cb _sb_set $w.list.sby v]
	if {$use_ttk} {
		$w_list configure -relief flat -highlightthickness 0 -borderwidth 0
	}
	pack $w_list -fill both -expand 1
	grid $w.list -sticky nswe -padx {20 5} -columnspan 2
	bind $w_list <Any-Motion>  [cb _show_tooltip @%x,%y]
	bind $w_list <Any-Enter>   [cb _hide_tooltip]
	bind $w_list <Any-Leave>   [cb _hide_tooltip]
	bind $w_list <Destroy>     [cb _hide_tooltip]

	grid columnconfigure $w 1 -weight 1
	if {$is_detached} {
		grid rowconfigure $w 3 -weight 1
	} else {
		grid rowconfigure $w 2 -weight 1
	}

	trace add variable @revtype write [cb _select]
	bind $w_filter <Key-Return> [list focus $w_list]\;break
	bind $w_filter <Key-Down>   [list focus $w_list]

	set fmt list
	append fmt { %(refname)}
	append fmt { [list}
	append fmt { %(objecttype)}
	append fmt { %(objectname)}
	append fmt { [concat %(taggername) %(authorname)]}
	append fmt { [reformat_date [concat %(taggerdate) %(authordate)]]}
	append fmt { %(subject)}
	append fmt {] [list}
	append fmt { %(*objecttype)}
	append fmt { %(*objectname)}
	append fmt { %(*authorname)}
	append fmt { [reformat_date %(*authordate)]}
	append fmt { %(*subject)}
	append fmt {]}
	set all_refn [list]
	set fr_fd [git_read for-each-ref \
		--tcl \
		--sort=-taggerdate \
		--format=$fmt \
		refs/heads \
		refs/remotes \
		refs/tags \
		]
	fconfigure $fr_fd -translation lf -encoding utf-8
	while {[gets $fr_fd line] > 0} {
		set line [eval $line]
		if {[lindex $line 1 0] eq {tag}} {
			if {[lindex $line 2 0] eq {commit}} {
				set sha1 [lindex $line 2 1]
			} else {
				continue
			}
		} elseif {[lindex $line 1 0] eq {commit}} {
			set sha1 [lindex $line 1 1]
		} else {
			continue
		}
		set refn [lindex $line 0]
		set tip_data($refn) [lrange $line 1 end]
		lappend cmt_refn($sha1) $refn
		lappend all_refn $refn
	}
	close $fr_fd

	if {$unmerged_only} {
		set fr_fd [git_read rev-list --all ^$::HEAD]
		while {[gets $fr_fd sha1] > 0} {
			if {[catch {set rlst $cmt_refn($sha1)}]} continue
			foreach refn $rlst {
				set inc($refn) 1
			}
		}
		close $fr_fd
	} else {
		foreach refn $all_refn {
			set inc($refn) 1
		}
	}

	set spec_head [list]
	foreach name [load_all_heads] {
		set refn refs/heads/$name
		if {[info exists inc($refn)]} {
			lappend spec_head [list $name $refn]
		}
	}

	set spec_trck [list]
	foreach spec [all_tracking_branches] {
		set refn [lindex $spec 0]
		if {[info exists inc($refn)]} {
			regsub ^refs/(heads|remotes)/ $refn {} name
			lappend spec_trck [concat $name $spec]
		}
	}

	set spec_tag [list]
	foreach name [load_all_tags] {
		set refn refs/tags/$name
		if {[info exists inc($refn)]} {
			lappend spec_tag [list $name $refn]
		}
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
	global NS use_ttk
	if {![winfo exists $w.none_r]} {
		${NS}::radiobutton $w.none_r \
			-value none \
			-variable @revtype
		if {!$use_ttk} {$w.none_r configure -anchor w}
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
		set msg [strcat [mc "Invalid revision: %s" [get $this]] "\n\n$err"]
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
			error [mc "No revision selected."]
		}
	}

	expr {
		if {$c_expr ne {}} {
			return $c_expr
		} else {
			error [mc "Revision expression is empty."]
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
	global NS
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
			${NS}::scrollbar $sb -orient h -command [list $w_list xview]
			pack $sb -fill x -side bottom -before $w_list
		} else {
			${NS}::scrollbar $sb -orient v -command [list $w_list yview]
			pack $sb -fill y -side right -before $w_list
		}
		if {$old_focus ne {}} {
			update
			focus $old_focus
		}
	}

	catch {$sb set $first $last}
}

method _show_tooltip {pos} {
	if {$tooltip_wm ne {}} {
		_open_tooltip $this
	} elseif {$tooltip_timer eq {}} {
		set tooltip_timer [after 1000 [cb _open_tooltip]]
	}
}

method _open_tooltip {} {
	global remote_url

	set tooltip_timer {}
	set pos_x [winfo pointerx $w_list]
	set pos_y [winfo pointery $w_list]
	if {[winfo containing $pos_x $pos_y] ne $w_list} {
		_hide_tooltip $this
		return
	}

	set pos @[join [list \
		[expr {$pos_x - [winfo rootx $w_list]}] \
		[expr {$pos_y - [winfo rooty $w_list]}]] ,]
	set lno [$w_list index $pos]
	if {$lno eq {}} {
		_hide_tooltip $this
		return
	}

	set spec [lindex $cur_specs $lno]
	set refn [lindex $spec 1]
	if {$refn eq {}} {
		_hide_tooltip $this
		return
	}

	if {$tooltip_wm eq {}} {
		set tooltip_wm [toplevel $w_list.tooltip -borderwidth 1]
		wm overrideredirect $tooltip_wm 1
		wm transient $tooltip_wm [winfo toplevel $w_list]
		set tooltip_t $tooltip_wm.label
		text $tooltip_t \
			-takefocus 0 \
			-highlightthickness 0 \
			-relief flat \
			-borderwidth 0 \
			-wrap none \
			-background lightyellow \
			-foreground black
		$tooltip_t tag conf section_header -font font_uibold
		bind $tooltip_wm <Escape> [cb _hide_tooltip]
		pack $tooltip_t
	} else {
		$tooltip_t conf -state normal
		$tooltip_t delete 0.0 end
	}

	set data $tip_data($refn)
	if {[lindex $data 0 0] eq {tag}} {
		set tag  [lindex $data 0]
		if {[lindex $data 1 0] eq {commit}} {
			set cmit [lindex $data 1]
		} else {
			set cmit {}
		}
	} elseif {[lindex $data 0 0] eq {commit}} {
		set tag  {}
		set cmit [lindex $data 0]
	}

	$tooltip_t insert end [lindex $spec 0]
	set last [_reflog_last $this [lindex $spec 1]]
	if {$last ne {}} {
		$tooltip_t insert end "\n"
		$tooltip_t insert end [mc "Updated"]
		$tooltip_t insert end " $last"
	}
	$tooltip_t insert end "\n"

	if {$tag ne {}} {
		$tooltip_t insert end "\n"
		$tooltip_t insert end [mc "Tag"] section_header
		$tooltip_t insert end "  [lindex $tag 1]\n"
		$tooltip_t insert end [lindex $tag 2]
		$tooltip_t insert end " ([lindex $tag 3])\n"
		$tooltip_t insert end [lindex $tag 4]
		$tooltip_t insert end "\n"
	}

	if {$cmit ne {}} {
		$tooltip_t insert end "\n"
		$tooltip_t insert end [mc "Commit@@noun"] section_header
		$tooltip_t insert end "  [lindex $cmit 1]\n"
		$tooltip_t insert end [lindex $cmit 2]
		$tooltip_t insert end " ([lindex $cmit 3])\n"
		$tooltip_t insert end [lindex $cmit 4]
	}

	if {[llength $spec] > 2} {
		$tooltip_t insert end "\n"
		$tooltip_t insert end [mc "Remote"] section_header
		$tooltip_t insert end "  [lindex $spec 2]\n"
		$tooltip_t insert end [mc "URL"]
		$tooltip_t insert end " $remote_url([lindex $spec 2])\n"
		$tooltip_t insert end [mc "Branch"]
		$tooltip_t insert end " [lindex $spec 3]"
	}

	$tooltip_t conf -state disabled
	_position_tooltip $this
}

method _reflog_last {name} {
	if {[info exists reflog_last($name)]} {
		return reflog_last($name)
	}

	set last {}
	if {[catch {set last [file mtime [gitdir $name]]}]
	&& ![catch {set g [open [gitdir logs $name] r]}]} {
		fconfigure $g -translation binary
		while {[gets $g line] >= 0} {
			if {[regexp {> ([1-9][0-9]*) } $line line when]} {
				set last $when
			}
		}
		close $g
	}

	if {$last ne {}} {
		set last [format_date $last]
	}
	set reflog_last($name) $last
	return $last
}

method _position_tooltip {} {
	set max_h [lindex [split [$tooltip_t index end] .] 0]
	set max_w 0
	for {set i 1} {$i <= $max_h} {incr i} {
		set c [lindex [split [$tooltip_t index "$i.0 lineend"] .] 1]
		if {$c > $max_w} {set max_w $c}
	}
	$tooltip_t conf -width $max_w -height $max_h

	set req_w [winfo reqwidth  $tooltip_t]
	set req_h [winfo reqheight $tooltip_t]
	set pos_x [expr {[winfo pointerx .] +  5}]
	set pos_y [expr {[winfo pointery .] + 10}]

	set g "${req_w}x${req_h}"
	if {[tk windowingsystem] eq "win32" || $pos_x >= 0} {append g +}
	append g $pos_x
	if {[tk windowingsystem] eq "win32" || $pos_y >= 0} {append g +}
	append g $pos_y

	wm geometry $tooltip_wm $g
	raise $tooltip_wm
}

method _hide_tooltip {} {
	if {$tooltip_wm ne {}} {
		destroy $tooltip_wm
		set tooltip_wm {}
	}
	if {$tooltip_timer ne {}} {
		after cancel $tooltip_timer
		set tooltip_timer {}
	}
}

}
