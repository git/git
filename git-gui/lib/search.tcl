# incremental search panel
# based on code from gitk, Copyright (C) Paul Mackerras

class searchbar {

field w
field ctext

field searchstring   {}
field regexpsearch
field default_regexpsearch
field casesensitive
field default_casesensitive
field smartcase
field searchdirn     -forwards

field history
field history_index

field smarktop
field smarkbot

constructor new {i_w i_text args} {
	set w      $i_w
	set ctext  $i_text

	set default_regexpsearch [is_config_true gui.search.regexp]
	switch -- [get_config gui.search.case] {
	no {
		set default_casesensitive 0
		set smartcase 0
	}
	smart {
		set default_casesensitive 0
		set smartcase 1
	}
	yes -
	default {
		set default_casesensitive 1
		set smartcase 0
	}
	}

	set history [list]

	ttk::frame  $w
	ttk::label  $w.l       -text [mc Find:]
	tentry  $w.ent -textvariable ${__this}::searchstring -background lightgreen
	ttk::button $w.bn      -text [mc Next] -command [cb find_next]
	ttk::button $w.bp      -text [mc Prev] -command [cb find_prev]
	ttk::checkbutton $w.re -text [mc RegExp] \
		-variable ${__this}::regexpsearch -command [cb _incrsearch]
	ttk::checkbutton $w.cs -text [mc Case] \
		-variable ${__this}::casesensitive -command [cb _incrsearch]
	pack   $w.l   -side left
	pack   $w.cs  -side right
	pack   $w.re  -side right
	pack   $w.bp  -side right
	pack   $w.bn  -side right
	pack   $w.ent -side left -expand 1 -fill x

	eval grid conf $w -sticky we $args
	grid remove $w

	trace add variable searchstring write [cb _incrsearch_cb]
	bind $w.ent <Return> [cb find_next]
	bind $w.ent <Shift-Return> [cb find_prev]
	bind $w.ent <Key-Up>   [cb _prev_search]
	bind $w.ent <Key-Down> [cb _next_search]
	
	bind $w <Destroy> [list delete_this $this]
	return $this
}

method show {} {
	if {![visible $this]} {
		grid $w
		$w.ent delete 0 end
		set regexpsearch  $default_regexpsearch
		set casesensitive $default_casesensitive
		set history_index [llength $history]
	}
	focus -force $w.ent
}

method hide {} {
	if {[visible $this]} {
		focus $ctext
		grid remove $w
		_save_search $this
	}
}

method visible {} {
	return [winfo ismapped $w]
}

method editor {} {
	return $w.ent
}

method _get_new_anchor {} {
	# use start of selection if it is visible,
	# or the bounds of the visible area
	set top    [$ctext index @0,0]
	set bottom [$ctext index @0,[winfo height $ctext]]
	set sel    [$ctext tag ranges sel]
	if {$sel ne {}} {
		set spos [lindex $sel 0]
		if {[lindex $spos 0] >= [lindex $top 0] &&
		    [lindex $spos 0] <= [lindex $bottom 0]} {
			return $spos
		}
	}
	if {$searchdirn eq "-forwards"} {
		return $top
	} else {
		return $bottom
	}
}

method _get_wrap_anchor {dir} {
	if {$dir eq "-forwards"} {
		return 1.0
	} else {
		return end
	}
}

method _do_search {start {mlenvar {}} {dir {}} {endbound {}}} {
	set cmd [list $ctext search]
	if {$mlenvar ne {}} {
		upvar $mlenvar mlen
		lappend cmd -count mlen
	}
	if {$regexpsearch} {
		lappend cmd -regexp
	}
	if {!$casesensitive} {
		lappend cmd -nocase
	}
	if {$dir eq {}} {
		set dir $searchdirn
	}
	lappend cmd $dir -- $searchstring
	if {[catch {
		if {$endbound ne {}} {
			set here [eval $cmd [list $start] [list $endbound]]
		} else {
			set here [eval $cmd [list $start]]
			if {$here eq {}} {
				set here [eval $cmd [_get_wrap_anchor $this $dir]]
			}
		}
	} err]} { set here {} }
	return $here
}

method _incrsearch_cb {name ix op} {
	after idle [cb _incrsearch]
}

method _incrsearch {} {
	$ctext tag remove found 1.0 end
	if {[catch {$ctext index anchor}]} {
		$ctext mark set anchor [_get_new_anchor $this]
	}
	if {$searchstring ne {}} {
		if {$smartcase && [regexp {[[:upper:]]} $searchstring]} {
			set casesensitive 1
		}
		set here [_do_search $this anchor mlen]
		if {$here ne {}} {
			$ctext see $here
			$ctext tag remove sel 1.0 end
			$ctext tag add sel $here "$here + $mlen c"
			#$w.ent configure -background lightgreen
			$w.ent state !pressed
			_set_marks $this 1
		} else {
			#$w.ent configure -background lightpink
			$w.ent state pressed
		}
	} elseif {$smartcase} {
		# clearing the field resets the smart case detection
		set casesensitive 0
	}
}

method _save_search {} {
	if {$searchstring eq {}} {
		return
	}
	if {[llength $history] > 0} {
		foreach {s_regexp s_case s_expr} [lindex $history end] break
	} else {
		set s_regexp $regexpsearch
		set s_case   $casesensitive
		set s_expr   ""
	}
	if {$searchstring eq $s_expr} {
		# update modes
		set history [lreplace $history end end \
				[list $regexpsearch $casesensitive $searchstring]]
	} else {
		lappend history [list $regexpsearch $casesensitive $searchstring]
	}
	set history_index [llength $history]
}

method _prev_search {} {
	if {$history_index > 0} {
		incr history_index -1
		foreach {s_regexp s_case s_expr} [lindex $history $history_index] break
		$w.ent delete 0 end
		$w.ent insert 0 $s_expr
		set regexpsearch $s_regexp
		set casesensitive $s_case
	}
}

method _next_search {} {
	if {$history_index < [llength $history]} {
		incr history_index
	}
	if {$history_index < [llength $history]} {
		foreach {s_regexp s_case s_expr} [lindex $history $history_index] break
	} else {
		set s_regexp $default_regexpsearch
		set s_case   $default_casesensitive
		set s_expr   ""
	}
	$w.ent delete 0 end
	$w.ent insert 0 $s_expr
	set regexpsearch $s_regexp
	set casesensitive $s_case
}

method find_prev {} {
	find_next $this -backwards
}

method find_next {{dir -forwards}} {
	focus $w.ent
	$w.ent icursor end
	set searchdirn $dir
	$ctext mark unset anchor
	if {$searchstring ne {}} {
		_save_search $this
		set start [_get_new_anchor $this]
		if {$dir eq "-forwards"} {
			set start "$start + 1c"
		}
		set match [_do_search $this $start mlen]
		$ctext tag remove sel 1.0 end
		if {$match ne {}} {
			$ctext see $match
			$ctext tag add sel $match "$match + $mlen c"
		}
	}
}

method _mark_range {first last} {
	set mend $first.0
	while {1} {
		set match [_do_search $this $mend mlen -forwards $last.end]
		if {$match eq {}} break
		set mend "$match + $mlen c"
		$ctext tag add found $match $mend
	}
}

method _set_marks {doall} {
	set topline [lindex [split [$ctext index @0,0] .] 0]
	set botline [lindex [split [$ctext index @0,[winfo height $ctext]] .] 0]
	if {$doall || $botline < $smarktop || $topline > $smarkbot} {
		# no overlap with previous
		_mark_range $this $topline $botline
		set smarktop $topline
		set smarkbot $botline
	} else {
		if {$topline < $smarktop} {
			_mark_range $this $topline [expr {$smarktop-1}]
			set smarktop $topline
		}
		if {$botline > $smarkbot} {
			_mark_range $this [expr {$smarkbot+1}] $botline
			set smarkbot $botline
		}
	}
}

method scrolled {} {
	if {$searchstring ne {}} {
		after idle [cb _set_marks 0]
	}
}

}
