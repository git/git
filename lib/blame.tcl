# git-gui blame viewer
# Copyright (C) 2006, 2007 Shawn Pearce

class blame {

image create photo ::blame::img_back_arrow -data {R0lGODlhGAAYAIUAAPwCBEzKXFTSZIz+nGzmhGzqfGTidIT+nEzGXHTqhGzmfGzifFzadETCVES+VARWDFzWbHzyjAReDGTadFTOZDSyRDyyTCymPARaFGTedFzSbDy2TCyqRCyqPARaDAyCHES6VDy6VCyiPAR6HCSeNByWLARyFARiDARqFGTifARiFAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAALAAAAAAYABgAAAajQIBwSCwaj8ikcsk0BppJwRPqHEypQwHBis0WDAdEFyBIKBaMAKLBdjQeSkFBYTBAIvgEoS6JmhUTEwIUDQ4VFhcMGEhyCgoZExoUaxsWHB0THkgfAXUGAhoBDSAVFR0XBnCbDRmgog0hpSIiDJpJIyEQhBUcJCIlwA22SSYVogknEg8eD82qSigdDSknY0IqJQXPYxIl1dZCGNvWw+Dm510GQQAh/mhDcmVhdGVkIGJ5IEJNUFRvR0lGIFBybyB2ZXJzaW9uIDIuNQ0KqSBEZXZlbENvciAxOTk3LDE5OTguIEFsbCByaWdodHMgcmVzZXJ2ZWQuDQpodHRwOi8vd3d3LmRldmVsY29yLmNvbQA7}

# Persistant data (survives loads)
#
field history {}; # viewer history: {commit path}
field header    ; # array commit,key -> header field

# Tk UI control paths
#
field w          ; # top window in this viewer
field w_back     ; # our back button
field w_path     ; # label showing the current file path
field w_columns  ; # list of all column widgets in the viewer
field w_line     ; # text column: all line numbers
field w_amov     ; # text column: annotations + move tracking
field w_asim     ; # text column: annotations (simple computation)
field w_file     ; # text column: actual file data
field w_cviewer  ; # pane showing commit message
field status     ; # status mega-widget instance
field old_height ; # last known height of $w.file_pane

# Tk UI colors
#
variable active_color #c0edc5
variable group_colors {
	#d6d6d6
	#e1e1e1
	#ececec
}

# Switches for original location detection
#
variable original_options [list -C -C]
if {[git-version >= 1.5.3]} {
	lappend original_options -w ; # ignore indentation changes
}

# Current blame data; cleared/reset on each load
#
field commit               ; # input commit to blame
field path                 ; # input filename to view in $commit

field current_fd        {} ; # background process running
field highlight_line    -1 ; # current line selected
field highlight_column  {} ; # current commit column selected
field highlight_commit  {} ; # sha1 of commit selected

field total_lines       0  ; # total length of file
field blame_lines       0  ; # number of lines computed
field amov_data            ; # list of {commit origfile origline}
field asim_data            ; # list of {commit origfile origline}

field r_commit             ; # commit currently being parsed
field r_orig_line          ; # original line number
field r_final_line         ; # final line number
field r_line_count         ; # lines in this region

field tooltip_wm        {} ; # Current tooltip toplevel, if open
field tooltip_t         {} ; # Text widget in $tooltip_wm
field tooltip_timer     {} ; # Current timer event for our tooltip
field tooltip_commit    {} ; # Commit(s) in tooltip

constructor new {i_commit i_path} {
	global cursor_ptr
	variable active_color
	variable group_colors

	set commit $i_commit
	set path   $i_path

	make_toplevel top w
	wm title $top [append "[appname] ([reponame]): " [mc "File Viewer"]]

	frame $w.header -background gold
	label $w.header.commit_l \
		-text [mc "Commit:"] \
		-background gold \
		-anchor w \
		-justify left
	set w_back $w.header.commit_b
	label $w_back \
		-image ::blame::img_back_arrow \
		-borderwidth 0 \
		-relief flat \
		-state disabled \
		-background gold \
		-activebackground gold
	bind $w_back <Button-1> "
		if {\[$w_back cget -state\] eq {normal}} {
			[cb _history_menu]
		}
		"
	label $w.header.commit \
		-textvariable @commit \
		-background gold \
		-anchor w \
		-justify left
	label $w.header.path_l \
		-text [mc "File:"] \
		-background gold \
		-anchor w \
		-justify left
	set w_path $w.header.path
	label $w_path \
		-background gold \
		-anchor w \
		-justify left
	pack $w.header.commit_l -side left
	pack $w_back -side left
	pack $w.header.commit -side left
	pack $w_path -fill x -side right
	pack $w.header.path_l -side right

	panedwindow $w.file_pane -orient vertical
	frame $w.file_pane.out
	frame $w.file_pane.cm
	$w.file_pane add $w.file_pane.out \
		-sticky nsew \
		-minsize 100 \
		-height 100 \
		-width 100
	$w.file_pane add $w.file_pane.cm \
		-sticky nsew \
		-minsize 25 \
		-height 25 \
		-width 100

	set w_line $w.file_pane.out.linenumber_t
	text $w_line \
		-takefocus 0 \
		-highlightthickness 0 \
		-padx 0 -pady 0 \
		-background white -borderwidth 0 \
		-state disabled \
		-wrap none \
		-height 40 \
		-width 6 \
		-font font_diff
	$w_line tag conf linenumber -justify right -rmargin 5

	set w_amov $w.file_pane.out.amove_t
	text $w_amov \
		-takefocus 0 \
		-highlightthickness 0 \
		-padx 0 -pady 0 \
		-background white -borderwidth 0 \
		-state disabled \
		-wrap none \
		-height 40 \
		-width 5 \
		-font font_diff
	$w_amov tag conf author_abbr -justify right -rmargin 5
	$w_amov tag conf curr_commit
	$w_amov tag conf prior_commit -foreground blue -underline 1
	$w_amov tag bind prior_commit \
		<Button-1> \
		"[cb _load_commit $w_amov @amov_data @%x,%y];break"

	set w_asim $w.file_pane.out.asimple_t
	text $w_asim \
		-takefocus 0 \
		-highlightthickness 0 \
		-padx 0 -pady 0 \
		-background white -borderwidth 0 \
		-state disabled \
		-wrap none \
		-height 40 \
		-width 4 \
		-font font_diff
	$w_asim tag conf author_abbr -justify right
	$w_asim tag conf curr_commit
	$w_asim tag conf prior_commit -foreground blue -underline 1
	$w_asim tag bind prior_commit \
		<Button-1> \
		"[cb _load_commit $w_asim @asim_data @%x,%y];break"

	set w_file $w.file_pane.out.file_t
	text $w_file \
		-takefocus 0 \
		-highlightthickness 0 \
		-padx 0 -pady 0 \
		-background white -borderwidth 0 \
		-state disabled \
		-wrap none \
		-height 40 \
		-width 80 \
		-xscrollcommand [list $w.file_pane.out.sbx set] \
		-font font_diff

	set w_columns [list $w_amov $w_asim $w_line $w_file]

	scrollbar $w.file_pane.out.sbx \
		-orient h \
		-command [list $w_file xview]
	scrollbar $w.file_pane.out.sby \
		-orient v \
		-command [list scrollbar2many $w_columns yview]
	eval grid $w_columns $w.file_pane.out.sby -sticky nsew
	grid conf \
		$w.file_pane.out.sbx \
		-column [expr {[llength $w_columns] - 1}] \
		-sticky we
	grid columnconfigure \
		$w.file_pane.out \
		[expr {[llength $w_columns] - 1}] \
		-weight 1
	grid rowconfigure $w.file_pane.out 0 -weight 1

	set w_cviewer $w.file_pane.cm.t
	text $w_cviewer \
		-background white -borderwidth 0 \
		-state disabled \
		-wrap none \
		-height 10 \
		-width 80 \
		-xscrollcommand [list $w.file_pane.cm.sbx set] \
		-yscrollcommand [list $w.file_pane.cm.sby set] \
		-font font_diff
	$w_cviewer tag conf still_loading \
		-font font_uiitalic \
		-justify center
	$w_cviewer tag conf header_key \
		-tabs {3c} \
		-background $active_color \
		-font font_uibold
	$w_cviewer tag conf header_val \
		-background $active_color \
		-font font_ui
	$w_cviewer tag raise sel
	scrollbar $w.file_pane.cm.sbx \
		-orient h \
		-command [list $w_cviewer xview]
	scrollbar $w.file_pane.cm.sby \
		-orient v \
		-command [list $w_cviewer yview]
	pack $w.file_pane.cm.sby -side right -fill y
	pack $w.file_pane.cm.sbx -side bottom -fill x
	pack $w_cviewer -expand 1 -fill both

	set status [::status_bar::new $w.status]

	menu $w.ctxm -tearoff 0
	$w.ctxm add command \
		-label [mc "Copy Commit"] \
		-command [cb _copycommit]

	foreach i $w_columns {
		for {set g 0} {$g < [llength $group_colors]} {incr g} {
			$i tag conf color$g -background [lindex $group_colors $g]
		}

		$i conf -cursor $cursor_ptr
		$i conf -yscrollcommand [list many2scrollbar \
			$w_columns yview $w.file_pane.out.sby]
		bind $i <Button-1> "
			[cb _hide_tooltip]
			[cb _click $i @%x,%y]
			focus $i
		"
		bind $i <Any-Motion>  [cb _show_tooltip $i @%x,%y]
		bind $i <Any-Enter>   [cb _hide_tooltip]
		bind $i <Any-Leave>   [cb _hide_tooltip]
		bind_button3 $i "
			[cb _hide_tooltip]
			set cursorX %x
			set cursorY %y
			set cursorW %W
			tk_popup $w.ctxm %X %Y
		"
		bind $i <Shift-Tab> "[list focus $w_cviewer];break"
		bind $i <Tab>       "[list focus $w_cviewer];break"
	}

	foreach i [concat $w_columns $w_cviewer] {
		bind $i <Key-Up>        {catch {%W yview scroll -1 units};break}
		bind $i <Key-Down>      {catch {%W yview scroll  1 units};break}
		bind $i <Key-Left>      {catch {%W xview scroll -1 units};break}
		bind $i <Key-Right>     {catch {%W xview scroll  1 units};break}
		bind $i <Key-k>         {catch {%W yview scroll -1 units};break}
		bind $i <Key-j>         {catch {%W yview scroll  1 units};break}
		bind $i <Key-h>         {catch {%W xview scroll -1 units};break}
		bind $i <Key-l>         {catch {%W xview scroll  1 units};break}
		bind $i <Control-Key-b> {catch {%W yview scroll -1 pages};break}
		bind $i <Control-Key-f> {catch {%W yview scroll  1 pages};break}
	}

	bind $w_cviewer <Shift-Tab> "[list focus $w_file];break"
	bind $w_cviewer <Tab>       "[list focus $w_file];break"
	bind $w_cviewer <Button-1> [list focus $w_cviewer]
	bind $w_file    <Visibility> [list focus $w_file]

	grid configure $w.header -sticky ew
	grid configure $w.file_pane -sticky nsew
	grid configure $w.status -sticky ew
	grid columnconfigure $top 0 -weight 1
	grid rowconfigure $top 0 -weight 0
	grid rowconfigure $top 1 -weight 1
	grid rowconfigure $top 2 -weight 0

	set req_w [winfo reqwidth  $top]
	set req_h [winfo reqheight $top]
	set scr_h [expr {[winfo screenheight $top] - 100}]
	if {$req_w < 600} {set req_w 600}
	if {$req_h < $scr_h} {set req_h $scr_h}
	set g "${req_w}x${req_h}"
	wm geometry $top $g
	update

	set old_height [winfo height $w.file_pane]
	$w.file_pane sash place 0 \
		[lindex [$w.file_pane sash coord 0] 0] \
		[expr {int($old_height * 0.70)}]
	bind $w.file_pane <Configure> \
	"if {{$w.file_pane} eq {%W}} {[cb _resize %h]}"

	_load $this {}
}

method _load {jump} {
	variable group_colors

	_hide_tooltip $this

	if {$total_lines != 0 || $current_fd ne {}} {
		if {$current_fd ne {}} {
			catch {close $current_fd}
			set current_fd {}
		}

		foreach i $w_columns {
			$i conf -state normal
			$i delete 0.0 end
			foreach g [$i tag names] {
				if {[regexp {^g[0-9a-f]{40}$} $g]} {
					$i tag delete $g
				}
			}
			$i conf -state disabled
		}

		$w_cviewer conf -state normal
		$w_cviewer delete 0.0 end
		$w_cviewer conf -state disabled

		set highlight_line -1
		set highlight_column {}
		set highlight_commit {}
		set total_lines 0
	}

	if {$history eq {}} {
		$w_back conf -state disabled
	} else {
		$w_back conf -state normal
	}

	# Index 0 is always empty.  There is never line 0 as
	# we use only 1 based lines, as that matches both with
	# git-blame output and with Tk's text widget.
	#
	set amov_data [list [list]]
	set asim_data [list [list]]

	$status show [mc "Reading %s..." "$commit:[escape_path $path]"]
	$w_path conf -text [escape_path $path]
	if {$commit eq {}} {
		set fd [open $path r]
		fconfigure $fd -eofchar {}
	} else {
		set fd [git_read cat-file blob "$commit:$path"]
	}
	fconfigure $fd -blocking 0 -translation lf -encoding binary
	fileevent $fd readable [cb _read_file $fd $jump]
	set current_fd $fd
}

method _history_menu {} {
	set m $w.backmenu
	if {[winfo exists $m]} {
		$m delete 0 end
	} else {
		menu $m -tearoff 0
	}

	for {set i [expr {[llength $history] - 1}]
		} {$i >= 0} {incr i -1} {
		set e [lindex $history $i]
		set c [lindex $e 0]
		set f [lindex $e 1]

		if {[regexp {^[0-9a-f]{40}$} $c]} {
			set t [string range $c 0 8]...
		} elseif {$c eq {}} {
			set t {Working Directory}
		} else {
			set t $c
		}
		if {![catch {set summary $header($c,summary)}]} {
			append t " $summary"
			if {[string length $t] > 70} {
				set t [string range $t 0 66]...
			}
		}

		$m add command -label $t -command [cb _goback $i]
	}
	set X [winfo rootx $w_back]
	set Y [expr {[winfo rooty $w_back] + [winfo height $w_back]}]
	tk_popup $m $X $Y
}

method _goback {i} {
	set dat [lindex $history $i]
	set history [lrange $history 0 [expr {$i - 1}]]
	set commit [lindex $dat 0]
	set path [lindex $dat 1]
	_load $this [lrange $dat 2 5]
}

method _read_file {fd jump} {
	if {$fd ne $current_fd} {
		catch {close $fd}
		return
	}

	foreach i $w_columns {$i conf -state normal}
	while {[gets $fd line] >= 0} {
		regsub "\r\$" $line {} line
		incr total_lines
		lappend amov_data {}
		lappend asim_data {}

		if {$total_lines > 1} {
			foreach i $w_columns {$i insert end "\n"}
		}

		$w_line insert end "$total_lines" linenumber
		$w_file insert end "$line"
	}

	set ln_wc [expr {[string length $total_lines] + 2}]
	if {[$w_line cget -width] < $ln_wc} {
		$w_line conf -width $ln_wc
	}

	foreach i $w_columns {$i conf -state disabled}

	if {[eof $fd]} {
		close $fd

		# If we don't force Tk to update the widgets *right now*
		# none of our jump commands will cause a change in the UI.
		#
		update

		if {[llength $jump] == 1} {
			set highlight_line [lindex $jump 0]
			$w_file see "$highlight_line.0"
		} elseif {[llength $jump] == 4} {
			set highlight_column [lindex $jump 0]
			set highlight_line [lindex $jump 1]
			$w_file xview moveto [lindex $jump 2]
			$w_file yview moveto [lindex $jump 3]
		}

		_exec_blame $this $w_asim @asim_data \
			[list] \
			[mc "Loading copy/move tracking annotations..."]
	}
} ifdeleted { catch {close $fd} }

method _exec_blame {cur_w cur_d options cur_s} {
	lappend options --incremental
	if {$commit eq {}} {
		lappend options --contents $path
	} else {
		lappend options $commit
	}
	lappend options -- $path
	set fd [eval git_read --nice blame $options]
	fconfigure $fd -blocking 0 -translation lf -encoding binary
	fileevent $fd readable [cb _read_blame $fd $cur_w $cur_d]
	set current_fd $fd
	set blame_lines 0

	$status start \
		$cur_s \
		[mc "lines annotated"]
}

method _read_blame {fd cur_w cur_d} {
	upvar #0 $cur_d line_data
	variable group_colors
	variable original_options

	if {$fd ne $current_fd} {
		catch {close $fd}
		return
	}

	$cur_w conf -state normal
	while {[gets $fd line] >= 0} {
		if {[regexp {^([a-z0-9]{40}) (\d+) (\d+) (\d+)$} $line line \
			cmit original_line final_line line_count]} {
			set r_commit     $cmit
			set r_orig_line  $original_line
			set r_final_line $final_line
			set r_line_count $line_count
		} elseif {[string match {filename *} $line]} {
			set file [string range $line 9 end]
			set n    $r_line_count
			set lno  $r_final_line
			set oln  $r_orig_line
			set cmit $r_commit

			if {[regexp {^0{40}$} $cmit]} {
				set commit_abbr work
				set commit_type curr_commit
			} elseif {$cmit eq $commit} {
				set commit_abbr this
				set commit_type curr_commit
			} else {
				set commit_type prior_commit
				set commit_abbr [string range $cmit 0 3]
			}

			set author_abbr {}
			set a_name {}
			catch {set a_name $header($cmit,author)}
			while {$a_name ne {}} {
				if {$author_abbr ne {}
					&& [string index $a_name 0] eq {'}} {
					regsub {^'[^']+'\s+} $a_name {} a_name
				}
				if {![regexp {^([[:upper:]])} $a_name _a]} break
				append author_abbr $_a
				unset _a
				if {![regsub \
					{^[[:upper:]][^\s]*\s+} \
					$a_name {} a_name ]} break
			}
			if {$author_abbr eq {}} {
				set author_abbr { |}
			} else {
				set author_abbr [string range $author_abbr 0 3]
			}
			unset a_name

			set first_lno $lno
			while {
			   $first_lno > 1
			&& $cmit eq [lindex $line_data [expr {$first_lno - 1}] 0]
			&& $file eq [lindex $line_data [expr {$first_lno - 1}] 1]
			} {
				incr first_lno -1
			}

			set color {}
			if {$first_lno < $lno} {
				foreach g [$w_file tag names $first_lno.0] {
					if {[regexp {^color[0-9]+$} $g]} {
						set color $g
						break
					}
				}
			} else {
				set i [lsort [concat \
					[$w_file tag names "[expr {$first_lno - 1}].0"] \
					[$w_file tag names "[expr {$lno + $n}].0"] \
					]]
				for {set g 0} {$g < [llength $group_colors]} {incr g} {
					if {[lsearch -sorted -exact $i color$g] == -1} {
						set color color$g
						break
					}
				}
			}
			if {$color eq {}} {
				set color color0
			}

			while {$n > 0} {
				set lno_e "$lno.0 lineend + 1c"
				if {[lindex $line_data $lno] ne {}} {
					set g [lindex $line_data $lno 0]
					foreach i $w_columns {
						$i tag remove g$g $lno.0 $lno_e
					}
				}
				lset line_data $lno [list $cmit $file $oln]

				$cur_w delete $lno.0 "$lno.0 lineend"
				if {$lno == $first_lno} {
					$cur_w insert $lno.0 $commit_abbr $commit_type
				} elseif {$lno == [expr {$first_lno + 1}]} {
					$cur_w insert $lno.0 $author_abbr author_abbr
				} else {
					$cur_w insert $lno.0 { |}
				}

				foreach i $w_columns {
					if {$cur_w eq $w_amov} {
						for {set g 0} \
							{$g < [llength $group_colors]} \
							{incr g} {
							$i tag remove color$g $lno.0 $lno_e
						}
						$i tag add $color $lno.0 $lno_e
					}
					$i tag add g$cmit $lno.0 $lno_e
				}

				if {$highlight_column eq $cur_w} {
					if {$highlight_line == -1
					 && [lindex [$w_file yview] 0] == 0} {
						$w_file see $lno.0
						set highlight_line $lno
					}
					if {$highlight_line == $lno} {
						_showcommit $this $cur_w $lno
					}
				}

				incr n -1
				incr lno
				incr oln
				incr blame_lines
			}

			while {
			   $cmit eq [lindex $line_data $lno 0]
			&& $file eq [lindex $line_data $lno 1]
			} {
				$cur_w delete $lno.0 "$lno.0 lineend"

				if {$lno == $first_lno} {
					$cur_w insert $lno.0 $commit_abbr $commit_type
				} elseif {$lno == [expr {$first_lno + 1}]} {
					$cur_w insert $lno.0 $author_abbr author_abbr
				} else {
					$cur_w insert $lno.0 { |}
				}

				if {$cur_w eq $w_amov} {
					foreach i $w_columns {
						for {set g 0} \
							{$g < [llength $group_colors]} \
							{incr g} {
							$i tag remove color$g $lno.0 $lno_e
						}
						$i tag add $color $lno.0 $lno_e
					}
				}

				incr lno
			}

		} elseif {[regexp {^([a-z-]+) (.*)$} $line line key data]} {
			set header($r_commit,$key) $data
		}
	}
	$cur_w conf -state disabled

	if {[eof $fd]} {
		close $fd
		if {$cur_w eq $w_asim} {
			_exec_blame $this $w_amov @amov_data \
				$original_options \
				[mc "Loading original location annotations..."]
		} else {
			set current_fd {}
			$status stop [mc "Annotation complete."]
		}
	} else {
		$status update $blame_lines $total_lines
	}
} ifdeleted { catch {close $fd} }

method _click {cur_w pos} {
	set lno [lindex [split [$cur_w index $pos] .] 0]
	_showcommit $this $cur_w $lno
}

method _load_commit {cur_w cur_d pos} {
	upvar #0 $cur_d line_data
	set lno [lindex [split [$cur_w index $pos] .] 0]
	set dat [lindex $line_data $lno]
	if {$dat ne {}} {
		lappend history [list \
			$commit $path \
			$highlight_column \
			$highlight_line \
			[lindex [$w_file xview] 0] \
			[lindex [$w_file yview] 0] \
			]
		set commit [lindex $dat 0]
		set path   [lindex $dat 1]
		_load $this [list [lindex $dat 2]]
	}
}

method _showcommit {cur_w lno} {
	global repo_config
	variable active_color

	if {$highlight_commit ne {}} {
		foreach i $w_columns {
			$i tag conf g$highlight_commit -background {}
			$i tag lower g$highlight_commit
		}
	}

	if {$cur_w eq $w_asim} {
		set dat [lindex $asim_data $lno]
		set highlight_column $w_asim
	} else {
		set dat [lindex $amov_data $lno]
		set highlight_column $w_amov
	}

	$w_cviewer conf -state normal
	$w_cviewer delete 0.0 end

	if {$dat eq {}} {
		set cmit {}
		$w_cviewer insert end [mc "Loading annotation..."] still_loading
	} else {
		set cmit [lindex $dat 0]
		set file [lindex $dat 1]

		foreach i $w_columns {
			$i tag conf g$cmit -background $active_color
			$i tag raise g$cmit
		}

		set author_name {}
		set author_email {}
		set author_time {}
		catch {set author_name $header($cmit,author)}
		catch {set author_email $header($cmit,author-mail)}
		catch {set author_time [format_date $header($cmit,author-time)]}

		set committer_name {}
		set committer_email {}
		set committer_time {}
		catch {set committer_name $header($cmit,committer)}
		catch {set committer_email $header($cmit,committer-mail)}
		catch {set committer_time [format_date $header($cmit,committer-time)]}

		if {[catch {set msg $header($cmit,message)}]} {
			set msg {}
			catch {
				set fd [git_read cat-file commit $cmit]
				fconfigure $fd -encoding binary -translation lf
				if {[catch {set enc $repo_config(i18n.commitencoding)}]} {
					set enc utf-8
				}
				while {[gets $fd line] > 0} {
					if {[string match {encoding *} $line]} {
						set enc [string tolower [string range $line 9 end]]
					}
				}
				set msg [read $fd]
				close $fd

				set enc [tcl_encoding $enc]
				if {$enc ne {}} {
					set msg [encoding convertfrom $enc $msg]
					set author_name [encoding convertfrom $enc $author_name]
					set committer_name [encoding convertfrom $enc $committer_name]
					set header($cmit,author) $author_name
					set header($cmit,committer) $committer_name
					set header($cmit,summary) \
					[encoding convertfrom $enc $header($cmit,summary)]
				}
				set msg [string trim $msg]
			}
			set header($cmit,message) $msg
		}

		$w_cviewer insert end "commit $cmit\n" header_key
		$w_cviewer insert end [strcat [mc "Author:"] "\t"] header_key
		$w_cviewer insert end "$author_name $author_email" header_val
		$w_cviewer insert end "  $author_time\n" header_val

		$w_cviewer insert end [strcat [mc "Committer:"] "\t"] header_key
		$w_cviewer insert end "$committer_name $committer_email" header_val
		$w_cviewer insert end "  $committer_time\n" header_val

		if {$file ne $path} {
			$w_cviewer insert end [strcat [mc "Original File:"] "\t"] header_key
			$w_cviewer insert end "[escape_path $file]\n" header_val
		}

		$w_cviewer insert end "\n$msg"
	}
	$w_cviewer conf -state disabled

	set highlight_line $lno
	set highlight_commit $cmit

	if {[lsearch -exact $tooltip_commit $highlight_commit] != -1} {
		_hide_tooltip $this
	}
}

method _copycommit {} {
	set pos @$::cursorX,$::cursorY
	set lno [lindex [split [$::cursorW index $pos] .] 0]
	set dat [lindex $amov_data $lno]
	if {$dat ne {}} {
		clipboard clear
		clipboard append \
			-format STRING \
			-type STRING \
			-- [lindex $dat 0]
	}
}

method _show_tooltip {cur_w pos} {
	if {$tooltip_wm ne {}} {
		_open_tooltip $this $cur_w
	} elseif {$tooltip_timer eq {}} {
		set tooltip_timer [after 1000 [cb _open_tooltip $cur_w]]
	}
}

method _open_tooltip {cur_w} {
	set tooltip_timer {}
	set pos_x [winfo pointerx $cur_w]
	set pos_y [winfo pointery $cur_w]
	if {[winfo containing $pos_x $pos_y] ne $cur_w} {
		_hide_tooltip $this
		return
	}

	if {$tooltip_wm ne "$cur_w.tooltip"} {
		_hide_tooltip $this

		set tooltip_wm [toplevel $cur_w.tooltip -borderwidth 1]
		wm overrideredirect $tooltip_wm 1
		wm transient $tooltip_wm [winfo toplevel $cur_w]
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
		pack $tooltip_t
	} else {
		$tooltip_t conf -state normal
		$tooltip_t delete 0.0 end
	}

	set pos @[join [list \
		[expr {$pos_x - [winfo rootx $cur_w]}] \
		[expr {$pos_y - [winfo rooty $cur_w]}]] ,]
	set lno [lindex [split [$cur_w index $pos] .] 0]
	if {$cur_w eq $w_amov} {
		set dat [lindex $amov_data $lno]
		set org {}
	} else {
		set dat [lindex $asim_data $lno]
		set org [lindex $amov_data $lno]
	}

	if {$dat eq {}} {
		_hide_tooltip $this
		return
	}

	set cmit [lindex $dat 0]
	set tooltip_commit [list $cmit]

	set author_name {}
	set summary     {}
	set author_time {}
	catch {set author_name $header($cmit,author)}
	catch {set summary     $header($cmit,summary)}
	catch {set author_time [format_date $header($cmit,author-time)]}

	$tooltip_t insert end "commit $cmit\n"
	$tooltip_t insert end "$author_name  $author_time\n"
	$tooltip_t insert end "$summary"

	if {$org ne {} && [lindex $org 0] ne $cmit} {
		set save [$tooltip_t get 0.0 end]
		$tooltip_t delete 0.0 end

		set cmit [lindex $org 0]
		set file [lindex $org 1]
		lappend tooltip_commit $cmit

		set author_name {}
		set summary     {}
		set author_time {}
		catch {set author_name $header($cmit,author)}
		catch {set summary     $header($cmit,summary)}
		catch {set author_time [format_date $header($cmit,author-time)]}

		$tooltip_t insert end [strcat [mc "Originally By:"] "\n"] section_header
		$tooltip_t insert end "commit $cmit\n"
		$tooltip_t insert end "$author_name  $author_time\n"
		$tooltip_t insert end "$summary\n"

		if {$file ne $path} {
			$tooltip_t insert end [strcat [mc "In File:"] " "] section_header
			$tooltip_t insert end "$file\n"
		}

		$tooltip_t insert end "\n"
		$tooltip_t insert end [strcat [mc "Copied Or Moved Here By:"] "\n"] section_header
		$tooltip_t insert end $save
	}

	$tooltip_t conf -state disabled
	_position_tooltip $this
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
	if {$pos_x >= 0} {append g +}
	append g $pos_x
	if {$pos_y >= 0} {append g +}
	append g $pos_y

	wm geometry $tooltip_wm $g
	raise $tooltip_wm
}

method _hide_tooltip {} {
	if {$tooltip_wm ne {}} {
		destroy $tooltip_wm
		set tooltip_wm {}
		set tooltip_commit {}
	}
	if {$tooltip_timer ne {}} {
		after cancel $tooltip_timer
		set tooltip_timer {}
	}
}

method _resize {new_height} {
	set diff [expr {$new_height - $old_height}]
	if {$diff == 0} return

	set my [expr {[winfo height $w.file_pane] - 25}]
	set o [$w.file_pane sash coord 0]
	set ox [lindex $o 0]
	set oy [expr {[lindex $o 1] + $diff}]
	if {$oy < 0}   {set oy 0}
	if {$oy > $my} {set oy $my}
	$w.file_pane sash place 0 $ox $oy

	set old_height $new_height
}

}
