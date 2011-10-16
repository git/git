# incremental search panel
# based on code from gitk, Copyright (C) Paul Mackerras

class searchbar {

field w
field ctext

field searchstring   {}
field casesensitive  1
field searchdirn     -forwards

field smarktop
field smarkbot

constructor new {i_w i_text args} {
	global use_ttk NS
	set w      $i_w
	set ctext  $i_text

	${NS}::frame  $w
	${NS}::label  $w.l       -text [mc Find:]
	entry  $w.ent -textvariable ${__this}::searchstring -background lightgreen
	${NS}::button $w.bn      -text [mc Next] -command [cb find_next]
	${NS}::button $w.bp      -text [mc Prev] -command [cb find_prev]
	${NS}::checkbutton $w.cs -text [mc Case-Sensitive] \
		-variable ${__this}::casesensitive -command [cb _incrsearch]
	pack   $w.l   -side left
	pack   $w.cs  -side right
	pack   $w.bp  -side right
	pack   $w.bn  -side right
	pack   $w.ent -side left -expand 1 -fill x

	eval grid conf $w -sticky we $args
	grid remove $w

	trace add variable searchstring write [cb _incrsearch_cb]
	bind $w.ent <Return> [cb find_next]
	bind $w.ent <Shift-Return> [cb find_prev]
	
	bind $w <Destroy> [list delete_this $this]
	return $this
}

method show {} {
	if {![visible $this]} {
		grid $w
	}
	focus -force $w.ent
}

method hide {} {
	if {[visible $this]} {
		focus $ctext
		grid remove $w
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
	if {!$casesensitive} {
		lappend cmd -nocase
	}
	if {$dir eq {}} {
		set dir $searchdirn
	}
	lappend cmd $dir -- $searchstring
	if {$endbound ne {}} {
		set here [eval $cmd [list $start] [list $endbound]]
	} else {
		set here [eval $cmd [list $start]]
		if {$here eq {}} {
			set here [eval $cmd [_get_wrap_anchor $this $dir]]
		}
	}
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
		set here [_do_search $this anchor mlen]
		if {$here ne {}} {
			$ctext see $here
			$ctext tag remove sel 1.0 end
			$ctext tag add sel $here "$here + $mlen c"
			$w.ent configure -background lightgreen
			_set_marks $this 1
		} else {
			$w.ent configure -background lightpink
		}
	}
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
