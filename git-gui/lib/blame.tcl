# git-gui blame viewer
# Copyright (C) 2006, 2007 Shawn Pearce

proc show_blame {commit path} {
	global next_browser_id blame_status blame_data

	if {[winfo ismapped .]} {
		set w .browser[incr next_browser_id]
		set tl $w
		toplevel $w
	} else {
		set w {}
		set tl .
	}
	set blame_status($w) {Loading current file content...}

	label $w.path -text "$commit:$path" \
		-anchor w \
		-justify left \
		-borderwidth 1 \
		-relief sunken \
		-font font_uibold
	pack $w.path -side top -fill x

	frame $w.out
	text $w.out.loaded_t \
		-background white -borderwidth 0 \
		-state disabled \
		-wrap none \
		-height 40 \
		-width 1 \
		-font font_diff
	$w.out.loaded_t tag conf annotated -background grey

	text $w.out.linenumber_t \
		-background white -borderwidth 0 \
		-state disabled \
		-wrap none \
		-height 40 \
		-width 5 \
		-font font_diff
	$w.out.linenumber_t tag conf linenumber -justify right

	text $w.out.file_t \
		-background white -borderwidth 0 \
		-state disabled \
		-wrap none \
		-height 40 \
		-width 80 \
		-xscrollcommand [list $w.out.sbx set] \
		-font font_diff

	scrollbar $w.out.sbx -orient h -command [list $w.out.file_t xview]
	scrollbar $w.out.sby -orient v \
		-command [list scrollbar2many [list \
		$w.out.loaded_t \
		$w.out.linenumber_t \
		$w.out.file_t \
		] yview]
	grid \
		$w.out.linenumber_t \
		$w.out.loaded_t \
		$w.out.file_t \
		$w.out.sby \
		-sticky nsew
	grid conf $w.out.sbx -column 2 -sticky we
	grid columnconfigure $w.out 2 -weight 1
	grid rowconfigure $w.out 0 -weight 1
	pack $w.out -fill both -expand 1

	label $w.status -textvariable blame_status($w) \
		-anchor w \
		-justify left \
		-borderwidth 1 \
		-relief sunken
	pack $w.status -side bottom -fill x

	frame $w.cm
	text $w.cm.t \
		-background white -borderwidth 0 \
		-state disabled \
		-wrap none \
		-height 10 \
		-width 80 \
		-xscrollcommand [list $w.cm.sbx set] \
		-yscrollcommand [list $w.cm.sby set] \
		-font font_diff
	scrollbar $w.cm.sbx -orient h -command [list $w.cm.t xview]
	scrollbar $w.cm.sby -orient v -command [list $w.cm.t yview]
	pack $w.cm.sby -side right -fill y
	pack $w.cm.sbx -side bottom -fill x
	pack $w.cm.t -expand 1 -fill both
	pack $w.cm -side bottom -fill x

	menu $w.ctxm -tearoff 0
	$w.ctxm add command -label "Copy Commit" \
		-command "blame_copycommit $w \$cursorW @\$cursorX,\$cursorY"

	foreach i [list \
		$w.out.loaded_t \
		$w.out.linenumber_t \
		$w.out.file_t] {
		$i tag conf in_sel \
			-background [$i cget -foreground] \
			-foreground [$i cget -background]
		$i conf -yscrollcommand \
			[list many2scrollbar [list \
			$w.out.loaded_t \
			$w.out.linenumber_t \
			$w.out.file_t \
			] yview $w.out.sby]
		bind $i <Button-1> "
			blame_click {$w} \\
				$w.cm.t \\
				$w.out.linenumber_t \\
				$w.out.file_t \\
				$i @%x,%y
			focus $i
		"
		bind_button3 $i "
			set cursorX %x
			set cursorY %y
			set cursorW %W
			tk_popup $w.ctxm %X %Y
		"
	}

	foreach i [list \
		$w.out.loaded_t \
		$w.out.linenumber_t \
		$w.out.file_t \
		$w.cm.t] {
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

	bind $w.cm.t <Button-1> "focus $w.cm.t"
	bind $tl <Visibility> "focus $tl"
	bind $tl <Destroy> "
		array unset blame_status {$w}
		array unset blame_data $w,*
	"
	wm title $tl "[appname] ([reponame]): File Viewer"

	set blame_data($w,commit_count) 0
	set blame_data($w,commit_list) {}
	set blame_data($w,total_lines) 0
	set blame_data($w,blame_lines) 0
	set blame_data($w,highlight_commit) {}
	set blame_data($w,highlight_line) -1

	set cmd [list git cat-file blob "$commit:$path"]
	set fd [open "| $cmd" r]
	fconfigure $fd -blocking 0 -translation lf -encoding binary
	fileevent $fd readable [list read_blame_catfile \
		$fd $w $commit $path \
		$w.cm.t $w.out.loaded_t $w.out.linenumber_t $w.out.file_t]
}

proc read_blame_catfile {fd w commit path w_cmit w_load w_line w_file} {
	global blame_status blame_data

	if {![winfo exists $w_file]} {
		catch {close $fd}
		return
	}

	set n $blame_data($w,total_lines)
	$w_load conf -state normal
	$w_line conf -state normal
	$w_file conf -state normal
	while {[gets $fd line] >= 0} {
		regsub "\r\$" $line {} line
		incr n
		$w_load insert end "\n"
		$w_line insert end "$n\n" linenumber
		$w_file insert end "$line\n"
	}
	$w_load conf -state disabled
	$w_line conf -state disabled
	$w_file conf -state disabled
	set blame_data($w,total_lines) $n

	if {[eof $fd]} {
		close $fd
		blame_incremental_status $w
		set cmd [list git blame -M -C --incremental]
		lappend cmd $commit -- $path
		set fd [open "| $cmd" r]
		fconfigure $fd -blocking 0 -translation lf -encoding binary
		fileevent $fd readable [list read_blame_incremental $fd $w \
			$w_load $w_cmit $w_line $w_file]
	}
}

proc read_blame_incremental {fd w w_load w_cmit w_line w_file} {
	global blame_status blame_data

	if {![winfo exists $w_file]} {
		catch {close $fd}
		return
	}

	while {[gets $fd line] >= 0} {
		if {[regexp {^([a-z0-9]{40}) (\d+) (\d+) (\d+)$} $line line \
			cmit original_line final_line line_count]} {
			set blame_data($w,commit) $cmit
			set blame_data($w,original_line) $original_line
			set blame_data($w,final_line) $final_line
			set blame_data($w,line_count) $line_count

			if {[catch {set g $blame_data($w,$cmit,order)}]} {
				$w_line tag conf g$cmit
				$w_file tag conf g$cmit
				$w_line tag raise in_sel
				$w_file tag raise in_sel
				$w_file tag raise sel
				set blame_data($w,$cmit,order) $blame_data($w,commit_count)
				incr blame_data($w,commit_count)
				lappend blame_data($w,commit_list) $cmit
			}
		} elseif {[string match {filename *} $line]} {
			set file [string range $line 9 end]
			set n $blame_data($w,line_count)
			set lno $blame_data($w,final_line)
			set cmit $blame_data($w,commit)

			while {$n > 0} {
				if {[catch {set g g$blame_data($w,line$lno,commit)}]} {
					$w_load tag add annotated $lno.0 "$lno.0 lineend + 1c"
				} else {
					$w_line tag remove g$g $lno.0 "$lno.0 lineend + 1c"
					$w_file tag remove g$g $lno.0 "$lno.0 lineend + 1c"
				}

				set blame_data($w,line$lno,commit) $cmit
				set blame_data($w,line$lno,file) $file
				$w_line tag add g$cmit $lno.0 "$lno.0 lineend + 1c"
				$w_file tag add g$cmit $lno.0 "$lno.0 lineend + 1c"

				if {$blame_data($w,highlight_line) == -1} {
					if {[lindex [$w_file yview] 0] == 0} {
						$w_file see $lno.0
						blame_showcommit $w $w_cmit $w_line $w_file $lno
					}
				} elseif {$blame_data($w,highlight_line) == $lno} {
					blame_showcommit $w $w_cmit $w_line $w_file $lno
				}

				incr n -1
				incr lno
				incr blame_data($w,blame_lines)
			}

			set hc $blame_data($w,highlight_commit)
			if {$hc ne {}
				&& [expr {$blame_data($w,$hc,order) + 1}]
					== $blame_data($w,$cmit,order)} {
				blame_showcommit $w $w_cmit $w_line $w_file \
					$blame_data($w,highlight_line)
			}
		} elseif {[regexp {^([a-z-]+) (.*)$} $line line header data]} {
			set blame_data($w,$blame_data($w,commit),$header) $data
		}
	}

	if {[eof $fd]} {
		close $fd
		set blame_status($w) {Annotation complete.}
	} else {
		blame_incremental_status $w
	}
}

proc blame_incremental_status {w} {
	global blame_status blame_data

	set have  $blame_data($w,blame_lines)
	set total $blame_data($w,total_lines)
	set pdone 0
	if {$total} {set pdone [expr {100 * $have / $total}]}

	set blame_status($w) [format \
		"Loading annotations... %i of %i lines annotated (%2i%%)" \
		$have $total $pdone]
}

proc blame_click {w w_cmit w_line w_file cur_w pos} {
	set lno [lindex [split [$cur_w index $pos] .] 0]
	if {$lno eq {}} return

	$w_line tag remove in_sel 0.0 end
	$w_file tag remove in_sel 0.0 end
	$w_line tag add in_sel $lno.0 "$lno.0 + 1 line"
	$w_file tag add in_sel $lno.0 "$lno.0 + 1 line"

	blame_showcommit $w $w_cmit $w_line $w_file $lno
}

set blame_colors {
	#ff4040
	#ff40ff
	#4040ff
}

proc blame_showcommit {w w_cmit w_line w_file lno} {
	global blame_colors blame_data repo_config

	set cmit $blame_data($w,highlight_commit)
	if {$cmit ne {}} {
		set idx $blame_data($w,$cmit,order)
		set i 0
		foreach c $blame_colors {
			set h [lindex $blame_data($w,commit_list) [expr {$idx - 1 + $i}]]
			$w_line tag conf g$h -background white
			$w_file tag conf g$h -background white
			incr i
		}
	}

	$w_cmit conf -state normal
	$w_cmit delete 0.0 end
	if {[catch {set cmit $blame_data($w,line$lno,commit)}]} {
		set cmit {}
		$w_cmit insert end "Loading annotation..."
	} else {
		set idx $blame_data($w,$cmit,order)
		set i 0
		foreach c $blame_colors {
			set h [lindex $blame_data($w,commit_list) [expr {$idx - 1 + $i}]]
			$w_line tag conf g$h -background $c
			$w_file tag conf g$h -background $c
			incr i
		}

		set author_name {}
		set author_email {}
		set author_time {}
		catch {set author_name $blame_data($w,$cmit,author)}
		catch {set author_email $blame_data($w,$cmit,author-mail)}
		catch {set author_time [clock format $blame_data($w,$cmit,author-time)]}

		set committer_name {}
		set committer_email {}
		set committer_time {}
		catch {set committer_name $blame_data($w,$cmit,committer)}
		catch {set committer_email $blame_data($w,$cmit,committer-mail)}
		catch {set committer_time [clock format $blame_data($w,$cmit,committer-time)]}

		if {[catch {set msg $blame_data($w,$cmit,message)}]} {
			set msg {}
			catch {
				set fd [open "| git cat-file commit $cmit" r]
				fconfigure $fd -encoding binary -translation lf
				if {[catch {set enc $repo_config(i18n.commitencoding)}]} {
					set enc utf-8
				}
				while {[gets $fd line] > 0} {
					if {[string match {encoding *} $line]} {
						set enc [string tolower [string range $line 9 end]]
					}
				}
				set msg [encoding convertfrom $enc [read $fd]]
				set msg [string trim $msg]
				close $fd

				set author_name [encoding convertfrom $enc $author_name]
				set committer_name [encoding convertfrom $enc $committer_name]

				set blame_data($w,$cmit,author) $author_name
				set blame_data($w,$cmit,committer) $committer_name
			}
			set blame_data($w,$cmit,message) $msg
		}

		$w_cmit insert end "commit $cmit\n"
		$w_cmit insert end "Author: $author_name $author_email $author_time\n"
		$w_cmit insert end "Committer: $committer_name $committer_email $committer_time\n"
		$w_cmit insert end "Original File: [escape_path $blame_data($w,line$lno,file)]\n"
		$w_cmit insert end "\n"
		$w_cmit insert end $msg
	}
	$w_cmit conf -state disabled

	set blame_data($w,highlight_line) $lno
	set blame_data($w,highlight_commit) $cmit
}

proc blame_copycommit {w i pos} {
	global blame_data
	set lno [lindex [split [$i index $pos] .] 0]
	if {![catch {set commit $blame_data($w,line$lno,commit)}]} {
		clipboard clear
		clipboard append \
			-format STRING \
			-type STRING \
			-- $commit
	}
}
