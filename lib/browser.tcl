# git-gui tree browser
# Copyright (C) 2006, 2007 Shawn Pearce

set next_browser_id 0

proc new_browser {commit} {
	global next_browser_id cursor_ptr M1B
	global browser_commit browser_status browser_stack browser_path browser_busy

	if {[winfo ismapped .]} {
		set w .browser[incr next_browser_id]
		set tl $w
		toplevel $w
	} else {
		set w {}
		set tl .
	}
	set w_list $w.list.l
	set browser_commit($w_list) $commit
	set browser_status($w_list) {Starting...}
	set browser_stack($w_list) {}
	set browser_path($w_list) $browser_commit($w_list):
	set browser_busy($w_list) 1

	label $w.path -textvariable browser_path($w_list) \
		-anchor w \
		-justify left \
		-borderwidth 1 \
		-relief sunken \
		-font font_uibold
	pack $w.path -anchor w -side top -fill x

	frame $w.list
	text $w_list -background white -borderwidth 0 \
		-cursor $cursor_ptr \
		-state disabled \
		-wrap none \
		-height 20 \
		-width 70 \
		-xscrollcommand [list $w.list.sbx set] \
		-yscrollcommand [list $w.list.sby set]
	$w_list tag conf in_sel \
		-background [$w_list cget -foreground] \
		-foreground [$w_list cget -background]
	scrollbar $w.list.sbx -orient h -command [list $w_list xview]
	scrollbar $w.list.sby -orient v -command [list $w_list yview]
	pack $w.list.sbx -side bottom -fill x
	pack $w.list.sby -side right -fill y
	pack $w_list -side left -fill both -expand 1
	pack $w.list -side top -fill both -expand 1

	label $w.status -textvariable browser_status($w_list) \
		-anchor w \
		-justify left \
		-borderwidth 1 \
		-relief sunken
	pack $w.status -anchor w -side bottom -fill x

	bind $w_list <Button-1>        "browser_click 0 $w_list @%x,%y;break"
	bind $w_list <Double-Button-1> "browser_click 1 $w_list @%x,%y;break"
	bind $w_list <$M1B-Up>         "browser_parent $w_list;break"
	bind $w_list <$M1B-Left>       "browser_parent $w_list;break"
	bind $w_list <Up>              "browser_move -1 $w_list;break"
	bind $w_list <Down>            "browser_move 1 $w_list;break"
	bind $w_list <$M1B-Right>      "browser_enter $w_list;break"
	bind $w_list <Return>          "browser_enter $w_list;break"
	bind $w_list <Prior>           "browser_page -1 $w_list;break"
	bind $w_list <Next>            "browser_page 1 $w_list;break"
	bind $w_list <Left>            break
	bind $w_list <Right>           break

	bind $tl <Visibility> "focus $w"
	bind $tl <Destroy> "
		array unset browser_buffer $w_list
		array unset browser_files $w_list
		array unset browser_status $w_list
		array unset browser_stack $w_list
		array unset browser_path $w_list
		array unset browser_commit $w_list
		array unset browser_busy $w_list
	"
	wm title $tl "[appname] ([reponame]): File Browser"
	ls_tree $w_list $browser_commit($w_list) {}
}

proc browser_move {dir w} {
	global browser_files browser_busy

	if {$browser_busy($w)} return
	set lno [lindex [split [$w index in_sel.first] .] 0]
	incr lno $dir
	if {[lindex $browser_files($w) [expr {$lno - 1}]] ne {}} {
		$w tag remove in_sel 0.0 end
		$w tag add in_sel $lno.0 [expr {$lno + 1}].0
		$w see $lno.0
	}
}

proc browser_page {dir w} {
	global browser_files browser_busy

	if {$browser_busy($w)} return
	$w yview scroll $dir pages
	set lno [expr {int(
		  [lindex [$w yview] 0]
		* [llength $browser_files($w)]
		+ 1)}]
	if {[lindex $browser_files($w) [expr {$lno - 1}]] ne {}} {
		$w tag remove in_sel 0.0 end
		$w tag add in_sel $lno.0 [expr {$lno + 1}].0
		$w see $lno.0
	}
}

proc browser_parent {w} {
	global browser_files browser_status browser_path
	global browser_stack browser_busy

	if {$browser_busy($w)} return
	set info [lindex $browser_files($w) 0]
	if {[lindex $info 0] eq {parent}} {
		set parent [lindex $browser_stack($w) end-1]
		set browser_stack($w) [lrange $browser_stack($w) 0 end-2]
		if {$browser_stack($w) eq {}} {
			regsub {:.*$} $browser_path($w) {:} browser_path($w)
		} else {
			regsub {/[^/]+$} $browser_path($w) {} browser_path($w)
		}
		set browser_status($w) "Loading $browser_path($w)..."
		ls_tree $w [lindex $parent 0] [lindex $parent 1]
	}
}

proc browser_enter {w} {
	global browser_files browser_status browser_path
	global browser_commit browser_stack browser_busy

	if {$browser_busy($w)} return
	set lno [lindex [split [$w index in_sel.first] .] 0]
	set info [lindex $browser_files($w) [expr {$lno - 1}]]
	if {$info ne {}} {
		switch -- [lindex $info 0] {
		parent {
			browser_parent $w
		}
		tree {
			set name [lindex $info 2]
			set escn [escape_path $name]
			set browser_status($w) "Loading $escn..."
			append browser_path($w) $escn
			ls_tree $w [lindex $info 1] $name
		}
		blob {
			set name [lindex $info 2]
			set p {}
			foreach n $browser_stack($w) {
				append p [lindex $n 1]
			}
			append p $name
			show_blame $browser_commit($w) $p
		}
		}
	}
}

proc browser_click {was_double_click w pos} {
	global browser_files browser_busy

	if {$browser_busy($w)} return
	set lno [lindex [split [$w index $pos] .] 0]
	focus $w

	if {[lindex $browser_files($w) [expr {$lno - 1}]] ne {}} {
		$w tag remove in_sel 0.0 end
		$w tag add in_sel $lno.0 [expr {$lno + 1}].0
		if {$was_double_click} {
			browser_enter $w
		}
	}
}

proc ls_tree {w tree_id name} {
	global browser_buffer browser_files browser_stack browser_busy

	set browser_buffer($w) {}
	set browser_files($w) {}
	set browser_busy($w) 1

	$w conf -state normal
	$w tag remove in_sel 0.0 end
	$w delete 0.0 end
	if {$browser_stack($w) ne {}} {
		$w image create end \
			-align center -padx 5 -pady 1 \
			-name icon0 \
			-image file_uplevel
		$w insert end {[Up To Parent]}
		lappend browser_files($w) parent
	}
	lappend browser_stack($w) [list $tree_id $name]
	$w conf -state disabled

	set cmd [list git ls-tree -z $tree_id]
	set fd [open "| $cmd" r]
	fconfigure $fd -blocking 0 -translation binary -encoding binary
	fileevent $fd readable [list read_ls_tree $fd $w]
}

proc read_ls_tree {fd w} {
	global browser_buffer browser_files browser_status browser_busy

	if {![winfo exists $w]} {
		catch {close $fd}
		return
	}

	append browser_buffer($w) [read $fd]
	set pck [split $browser_buffer($w) "\0"]
	set browser_buffer($w) [lindex $pck end]

	set n [llength $browser_files($w)]
	$w conf -state normal
	foreach p [lrange $pck 0 end-1] {
		set info [split $p "\t"]
		set path [lindex $info 1]
		set info [split [lindex $info 0] { }]
		set type [lindex $info 1]
		set object [lindex $info 2]

		switch -- $type {
		blob {
			set image file_mod
		}
		tree {
			set image file_dir
			append path /
		}
		default {
			set image file_question
		}
		}

		if {$n > 0} {$w insert end "\n"}
		$w image create end \
			-align center -padx 5 -pady 1 \
			-name icon[incr n] \
			-image $image
		$w insert end [escape_path $path]
		lappend browser_files($w) [list $type $object $path]
	}
	$w conf -state disabled

	if {[eof $fd]} {
		close $fd
		set browser_status($w) Ready.
		set browser_busy($w) 0
		array unset browser_buffer $w
		if {$n > 0} {
			$w tag add in_sel 1.0 2.0
			focus -force $w
		}
	}
}
