# git-gui blame viewer
# Copyright (C) 2006, 2007 Shawn Pearce

class blame {

field commit  ; # input commit to blame
field path    ; # input filename to view in $commit

field w
field w_line
field w_load
field w_file
field w_cmit
field status

field highlight_line   -1 ; # current line selected
field highlight_commit {} ; # sha1 of commit selected

field total_lines       0  ; # total length of file
field blame_lines       0  ; # number of lines computed
field commit_count      0  ; # number of commits in $commit_list
field commit_list      {}  ; # list of commit sha1 in receipt order
field order                ; # array commit -> receipt order
field header               ; # array commit,key -> header field
field line_commit          ; # array line -> sha1 commit
field line_file            ; # array line -> file name

field r_commit      ; # commit currently being parsed
field r_orig_line   ; # original line number
field r_final_line  ; # final line number
field r_line_count  ; # lines in this region

constructor new {i_commit i_path} {
	set commit $i_commit
	set path   $i_path

	make_toplevel top w
	wm title $top "[appname] ([reponame]): File Viewer"
	set status "Loading $commit:$path..."

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

	label $w.status \
		-textvariable @status \
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
	$w.ctxm add command \
		-label "Copy Commit" \
		-command [cb _copycommit]

	set w_line $w.out.linenumber_t
	set w_load $w.out.loaded_t
	set w_file $w.out.file_t
	set w_cmit $w.cm.t

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
		bind $i <Button-1> "[cb _click $i @%x,%y]; focus $i"
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

	bind $w.cm.t <Button-1> [list focus $w.cm.t]
	bind $top <Visibility> [list focus $top]
	bind $top <Destroy> [list delete_this $this]

	if {$commit eq {}} {
		set fd [open $path r]
	} else {
		set cmd [list git cat-file blob "$commit:$path"]
		set fd [open "| $cmd" r]
	}
	fconfigure $fd -blocking 0 -translation lf -encoding binary
	fileevent $fd readable [cb _read_file $fd]
}

method _read_file {fd} {
	$w_load conf -state normal
	$w_line conf -state normal
	$w_file conf -state normal
	while {[gets $fd line] >= 0} {
		regsub "\r\$" $line {} line
		incr total_lines
		$w_load insert end "\n"
		$w_line insert end "$total_lines\n" linenumber
		$w_file insert end "$line\n"
	}
	$w_load conf -state disabled
	$w_line conf -state disabled
	$w_file conf -state disabled

	if {[eof $fd]} {
		close $fd
		_status $this
		set cmd [list git blame -M -C --incremental]
		if {$commit eq {}} {
			lappend cmd --contents $path
		} else {
			lappend cmd $commit
		}
		lappend cmd -- $path
		set fd [open "| $cmd" r]
		fconfigure $fd -blocking 0 -translation lf -encoding binary
		fileevent $fd readable [cb _read_blame $fd]
	}
} ifdeleted { catch {close $fd} }

method _read_blame {fd} {
	while {[gets $fd line] >= 0} {
		if {[regexp {^([a-z0-9]{40}) (\d+) (\d+) (\d+)$} $line line \
			cmit original_line final_line line_count]} {
			set r_commit     $cmit
			set r_orig_line  $original_line
			set r_final_line $final_line
			set r_line_count $line_count

			if {[catch {set g $order($cmit)}]} {
				$w_line tag conf g$cmit
				$w_file tag conf g$cmit
				$w_line tag raise in_sel
				$w_file tag raise in_sel
				$w_file tag raise sel
				set order($cmit) $commit_count
				incr commit_count
				lappend commit_list $cmit
			}
		} elseif {[string match {filename *} $line]} {
			set file [string range $line 9 end]
			set n    $r_line_count
			set lno  $r_final_line
			set cmit $r_commit

			while {$n > 0} {
				set lno_e "$lno.0 lineend + 1c"
				if {[catch {set g g$line_commit($lno)}]} {
					$w_load tag add annotated $lno.0 $lno_e
				} else {
					$w_line tag remove g$g $lno.0 $lno_e
					$w_file tag remove g$g $lno.0 $lno_e
				}

				set line_commit($lno) $cmit
				set line_file($lno)   $file
				$w_line tag add g$cmit $lno.0 $lno_e
				$w_file tag add g$cmit $lno.0 $lno_e

				if {$highlight_line == -1} {
					if {[lindex [$w_file yview] 0] == 0} {
						$w_file see $lno.0
						_showcommit $this $lno
					}
				} elseif {$highlight_line == $lno} {
					_showcommit $this $lno
				}

				incr n -1
				incr lno
				incr blame_lines
			}

			set hc $highlight_commit
			if {$hc ne {}
				&& [expr {$order($hc) + 1}] == $order($cmit)} {
				_showcommit $this $highlight_line
			}
		} elseif {[regexp {^([a-z-]+) (.*)$} $line line key data]} {
			set header($r_commit,$key) $data
		}
	}

	if {[eof $fd]} {
		close $fd
		set status {Annotation complete.}
	} else {
		_status $this
	}
} ifdeleted { catch {close $fd} }

method _status {} {
	set have  $blame_lines
	set total $total_lines
	set pdone 0
	if {$total} {set pdone [expr {100 * $have / $total}]}

	set status [format \
		"Loading annotations... %i of %i lines annotated (%2i%%)" \
		$have $total $pdone]
}

method _click {cur_w pos} {
	set lno [lindex [split [$cur_w index $pos] .] 0]
	if {$lno eq {}} return

	set lno_e "$lno.0 + 1 line"
	$w_line tag remove in_sel 0.0 end
	$w_file tag remove in_sel 0.0 end
	$w_line tag add in_sel $lno.0 $lno_e
	$w_file tag add in_sel $lno.0 $lno_e

	_showcommit $this $lno
}

variable blame_colors {
	#ff4040
	#ff40ff
	#4040ff
}

method _showcommit {lno} {
	global repo_config
	variable blame_colors

	if {$highlight_commit ne {}} {
		set idx $order($highlight_commit)
		set i 0
		foreach c $blame_colors {
			set h [lindex $commit_list [expr {$idx - 1 + $i}]]
			$w_line tag conf g$h -background white
			$w_file tag conf g$h -background white
			incr i
		}
	}

	$w_cmit conf -state normal
	$w_cmit delete 0.0 end
	if {[catch {set cmit $line_commit($lno)}]} {
		set cmit {}
		$w_cmit insert end "Loading annotation..."
	} else {
		set idx $order($cmit)
		set i 0
		foreach c $blame_colors {
			set h [lindex $commit_list [expr {$idx - 1 + $i}]]
			$w_line tag conf g$h -background $c
			$w_file tag conf g$h -background $c
			incr i
		}

		set author_name {}
		set author_email {}
		set author_time {}
		catch {set author_name $header($cmit,author)}
		catch {set author_email $header($cmit,author-mail)}
		catch {set author_time [clock format \
			$header($cmit,author-time) \
			-format {%Y-%m-%d %H:%M:%S}
		]}

		set committer_name {}
		set committer_email {}
		set committer_time {}
		catch {set committer_name $header($cmit,committer)}
		catch {set committer_email $header($cmit,committer-mail)}
		catch {set committer_time [clock format \
			$header($cmit,committer-time) \
			-format {%Y-%m-%d %H:%M:%S}
		]}

		if {[catch {set msg $header($cmit,message)}]} {
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

				set header($cmit,author) $author_name
				set header($cmit,committer) $committer_name
			}
			set header($cmit,message) $msg
		}

		$w_cmit insert end "commit $cmit
Author: $author_name $author_email  $author_time
Committer: $committer_name $committer_email  $committer_time
Original File: [escape_path $line_file($lno)]

$msg"
	}
	$w_cmit conf -state disabled

	set highlight_line $lno
	set highlight_commit $cmit
}

method _copycommit {} {
	set pos @$::cursorX,$::cursorY
	set lno [lindex [split [$::cursorW index $pos] .] 0]
	if {![catch {set commit $line_commit($lno)}]} {
		clipboard clear
		clipboard append \
			-format STRING \
			-type STRING \
			-- $commit
	}
}

}
