# git-gui tree browser
# Copyright (C) 2006, 2007 Shawn Pearce

class browser {

image create photo ::browser::img_parent  -data {R0lGODlhEAAQAIUAAPwCBBxSHBxOHMTSzNzu3KzCtBRGHCSKFIzCjLzSxBQ2FAxGHDzCLCyeHBQ+FHSmfAwuFBxKLDSCNMzizISyjJzOnDSyLAw+FAQSDAQeDBxWJAwmDAQOBKzWrDymNAQaDAQODAwaDDyKTFSyXFTGTEy6TAQCBAQKDAwiFBQyHAwSFAwmHAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAALAAAAAAQABAAAAZ1QIBwSCwaj0hiQCBICpcDQsFgGAaIguhhi0gohIsrQEDYMhiNrRfgeAQC5fMCAolIDhD2hFI5WC4YRBkaBxsOE2l/RxsHHA4dHmkfRyAbIQ4iIyQlB5NFGCAACiakpSZEJyinTgAcKSesACorgU4mJ6uxR35BACH+aENyZWF0ZWQgYnkgQk1QVG9HSUYgUHJvIHZlcnNpb24gMi41DQqpIERldmVsQ29yIDE5OTcsMTk5OC4gQWxsIHJpZ2h0cyByZXNlcnZlZC4NCmh0dHA6Ly93d3cuZGV2ZWxjb3IuY29tADs=}
image create photo ::browser::img_rblob   -data {R0lGODlhEAAQAIUAAPwCBFxaXNze3Ly2rJSWjPz+/Ozq7GxqbJyanPT29HRydMzOzDQyNIyKjERCROTi3Pz69PTy7Pzy7PTu5Ozm3LyqlJyWlJSSjJSOhOzi1LyulPz27PTq3PTm1OzezLyqjIyKhJSKfOzaxPz29OzizLyidIyGdIyCdOTOpLymhOzavOTStMTCtMS+rMS6pMSynMSulLyedAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAALAAAAAAQABAAAAaQQIAQECgajcNkQMBkDgKEQFK4LFgLhkMBIVUKroWEYlEgMLxbBKLQUBwc52HgAQ4LBo049atWQyIPA3pEdFcQEhMUFYNVagQWFxgZGoxfYRsTHB0eH5UJCJAYICEinUoPIxIcHCQkIiIllQYEGCEhJicoKYwPmiQeKisrKLFKLCwtLi8wHyUlMYwM0tPUDH5BACH+aENyZWF0ZWQgYnkgQk1QVG9HSUYgUHJvIHZlcnNpb24gMi41DQqpIERldmVsQ29yIDE5OTcsMTk5OC4gQWxsIHJpZ2h0cyByZXNlcnZlZC4NCmh0dHA6Ly93d3cuZGV2ZWxjb3IuY29tADs=}
image create photo ::browser::img_xblob   -data {R0lGODlhEAAQAIYAAPwCBFRWVFxaXNza3OTi3Nze3Ly2tJyanPz+/Ozq7GxubNzSxMzOzMTGxHRybDQyNLy+vHRydHx6fKSipISChIyKjGxqbERCRCwuLLy6vGRiZExKTCQiJAwKDLSytLy2rJSSlHx+fDw6PKyqrBQWFPTu5Ozm3LyulLS2tCQmJAQCBPTq3Ozi1MSynCwqLAQGBOTazOzizOzezLyqjBweHNzSvOzaxKyurHRuZNzOtLymhDw+PIyCdOzWvOTOpLyidNzKtOTStLyifMTCtMS+rLyedAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAALAAAAAAQABAAAAfZgACCAAEChYeGg4oCAwQFjgYBBwGKggEECJkICQoIkwADCwwNDY2mDA4Lng8QDhESsLARExQVDhYXGBkWExIaGw8cHR4SCQQfFQ8eFgUgIQEiwiMSBMYfGB4atwEXDyQd0wQlJicPKAHoFyIpJCoeDgMrLC0YKBsX6i4kL+4OMDEyZijr5oLGNxUqUCioEcPGDAwjPNyI6MEDChQjcOSwsUDHgw07RIgI4KCkAgs8cvTw8eOBogAxQtXIASTISiEuBwUYMoRIixYnZggpUgTDywdIkWJIitRPIAAh/mhDcmVhdGVkIGJ5IEJNUFRvR0lGIFBybyB2ZXJzaW9uIDIuNQ0KqSBEZXZlbENvciAxOTk3LDE5OTguIEFsbCByaWdodHMgcmVzZXJ2ZWQuDQpodHRwOi8vd3d3LmRldmVsY29yLmNvbQA7}
image create photo ::browser::img_tree    -data {R0lGODlhEAAQAIYAAPwCBAQCBExKTBwWHMzKzOzq7ERCRExGTCwqLARqnAQ+ZHR2dKyqrNTOzHx2fCQiJMTi9NTu9HzC3AxmnAQ+XPTm7Dy67DymzITC3IzG5AxypHRydKymrMzOzOzu7BweHByy9AyGtFyy1IzG3NTu/ARupFRSVByazBR6rAyGvFyuzJTK3MTm9BR+tAxWhHS61MTi7Pz+/IymvCxulBRelAx2rHS63Pz6/PTy9PTu9Nza3ISitBRupFSixNTS1CxqnDQyNMzGzOTi5MTCxMTGxGxubGxqbLy2vLSutGRiZLy6vLSytKyurDQuNFxaXKSipDw6PAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAALAAAAAAQABAAAAfDgACCAAECg4eIAAMEBQYHCImDBgkKCwwNBQIBBw4Bhw8QERITFJYEFQUFnoIPFhcYoRkaFBscHR4Ggh8gIRciEiMQJBkltCa6JyUoKSkXKhIrLCQYuQAPLS4TEyUhKb0qLzDVAjEFMjMuNBMoNcw21QY3ODkFOjs82RM1PfDzFRU3fOggcM7Fj2pAgggRokOHDx9DhhAZUqQaISBGhjwMEvEIkiIHEgUAkgSJkiNLmFSMJChAEydPGBSBwvJQgAc0/QQCACH+aENyZWF0ZWQgYnkgQk1QVG9HSUYgUHJvIHZlcnNpb24gMi41DQqpIERldmVsQ29yIDE5OTcsMTk5OC4gQWxsIHJpZ2h0cyByZXNlcnZlZC4NCmh0dHA6Ly93d3cuZGV2ZWxjb3IuY29tADs=}
image create photo ::browser::img_symlink -data {R0lGODlhEAAQAIQAAPwCBCwqLLSytLy+vERGRFRWVDQ2NKSmpAQCBKyurMTGxISChJyanHR2dIyKjGxubHRydGRmZIyOjFxeXHx6fAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAALAAAAAAQABAAAAVbICACwWieY1CibCCsrBkMb0zchSEcNYskCtqBBzshFkOGQFk0IRqOxqPBODRHCMhCQKteRc9FI/KQWGOIyFYgkDC+gPR4snCcfRGKOIKIgSMQE31+f4OEYCZ+IQAh/mhDcmVhdGVkIGJ5IEJNUFRvR0lGIFBybyB2ZXJzaW9uIDIuNQ0KqSBEZXZlbENvciAxOTk3LDE5OTguIEFsbCByaWdodHMgcmVzZXJ2ZWQuDQpodHRwOi8vd3d3LmRldmVsY29yLmNvbQA7}
image create photo ::browser::img_unknown -data {R0lGODlhEAAQAIUAAPwCBFxaXIyKjNTW1Nze3LS2tJyanER2RGS+VPz+/PTu5GxqbPz69BQ6BCxeLFSqRPT29HRydMzOzDQyNERmPKSypCRWHIyKhERCRDyGPKz2nESiLBxGHCyCHGxubPz6/PTy7Ozi1Ly2rKSipOzm3LyqlKSWhCRyFOzizLymhNTKtNzOvOzaxOTStPz27OzWvOTOpLSupLyedMS+rMS6pMSulLyqjLymfLyifAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAALAAAAAAQABAAAAamQIAQECgajcOkYEBoDgoBQyAJOCCuiENCsWBIh9aGw9F4HCARiXciRDQoBUnlYRlcIgsMG5CxXAgMGhscBRAEBRd7AB0eBBoIgxUfICEiikSPgyMMIAokJZcBkBybJgomIaBJAZoMpyCmqkMBFCcVCrgKKAwpoSorKqchKCwtvasIFBIhLiYvLzDHsxQNMcMKLDAwMqEz3jQ1NTY3ONyrE+jp6hN+QQAh/mhDcmVhdGVkIGJ5IEJNUFRvR0lGIFBybyB2ZXJzaW9uIDIuNQ0KqSBEZXZlbENvciAxOTk3LDE5OTguIEFsbCByaWdodHMgcmVzZXJ2ZWQuDQpodHRwOi8vd3d3LmRldmVsY29yLmNvbQA7}

field w
field browser_commit
field browser_path
field browser_files  {}
field browser_status [mc "Starting..."]
field browser_stack  {}
field browser_busy   1

field ls_buf     {}; # Buffered record output from ls-tree

constructor new {commit {path {}}} {
	global cursor_ptr M1B use_ttk NS
	make_dialog top w
	wm withdraw $top
	wm title $top [append "[appname] ([reponame]): " [mc "File Browser"]]

	if {$path ne {}} {
		if {[string index $path end] ne {/}} {
			append path /
		}
	}

	set browser_commit $commit
	set browser_path "$browser_commit:[escape_path $path]"

	${NS}::label $w.path \
		-textvariable @browser_path \
		-anchor w \
		-justify left \
		-font font_uibold
	if {!$use_ttk} { $w.path configure -borderwidth 1 -relief sunken}
	pack $w.path -anchor w -side top -fill x

	${NS}::frame $w.list
	set w_list $w.list.l
	text $w_list -background white -foreground black \
		-borderwidth 0 \
		-cursor $cursor_ptr \
		-state disabled \
		-wrap none \
		-height 20 \
		-width 70 \
		-xscrollcommand [list $w.list.sbx set] \
		-yscrollcommand [list $w.list.sby set]
	rmsel_tag $w_list
	${NS}::scrollbar $w.list.sbx -orient h -command [list $w_list xview]
	${NS}::scrollbar $w.list.sby -orient v -command [list $w_list yview]
	pack $w.list.sbx -side bottom -fill x
	pack $w.list.sby -side right -fill y
	pack $w_list -side left -fill both -expand 1
	pack $w.list -side top -fill both -expand 1

	${NS}::label $w.status \
		-textvariable @browser_status \
		-anchor w \
		-justify left
	if {!$use_ttk} { $w.status configure -borderwidth 1 -relief sunken}
	pack $w.status -anchor w -side bottom -fill x

	bind $w_list <Button-1>        "[cb _click 0 @%x,%y];break"
	bind $w_list <Double-Button-1> "[cb _click 1 @%x,%y];break"
	bind $w_list <$M1B-Up>         "[cb _parent]        ;break"
	bind $w_list <$M1B-Left>       "[cb _parent]        ;break"
	bind $w_list <Up>              "[cb _move -1]       ;break"
	bind $w_list <Down>            "[cb _move  1]       ;break"
	bind $w_list <$M1B-Right>      "[cb _enter]         ;break"
	bind $w_list <Return>          "[cb _enter]         ;break"
	bind $w_list <Prior>           "[cb _page -1]       ;break"
	bind $w_list <Next>            "[cb _page  1]       ;break"
	bind $w_list <Left>            break
	bind $w_list <Right>           break

	bind $w_list <Visibility> [list focus $w_list]
	wm deiconify $top
	set w $w_list
	if {$path ne {}} {
		_ls $this $browser_commit:$path $path
	} else {
		_ls $this $browser_commit $path
	}
	return $this
}

method _move {dir} {
	if {$browser_busy} return
	set lno [lindex [split [$w index in_sel.first] .] 0]
	incr lno $dir
	if {[lindex $browser_files [expr {$lno - 1}]] ne {}} {
		$w tag remove in_sel 0.0 end
		$w tag add in_sel $lno.0 [expr {$lno + 1}].0
		$w see $lno.0
	}
}

method _page {dir} {
	if {$browser_busy} return
	$w yview scroll $dir pages
	set lno [expr {int(
		  [lindex [$w yview] 0]
		* [llength $browser_files]
		+ 1)}]
	if {[lindex $browser_files [expr {$lno - 1}]] ne {}} {
		$w tag remove in_sel 0.0 end
		$w tag add in_sel $lno.0 [expr {$lno + 1}].0
		$w see $lno.0
	}
}

method _parent {} {
	if {$browser_busy} return
	set info [lindex $browser_files 0]
	if {[lindex $info 0] eq {parent}} {
		set parent [lindex $browser_stack end-1]
		set browser_stack [lrange $browser_stack 0 end-2]
		if {$browser_stack eq {}} {
			regsub {:.*$} $browser_path {:} browser_path
		} else {
			regsub {/[^/]+/$} $browser_path {/} browser_path
		}
		set browser_status [mc "Loading %s..." $browser_path]
		_ls $this [lindex $parent 0] [lindex $parent 1]
	}
}

method _enter {} {
	if {$browser_busy} return
	set lno [lindex [split [$w index in_sel.first] .] 0]
	set info [lindex $browser_files [expr {$lno - 1}]]
	if {$info ne {}} {
		switch -- [lindex $info 0] {
		parent {
			_parent $this
		}
		tree {
			set name [lindex $info 2]
			set escn [escape_path $name]
			set browser_status [mc "Loading %s..." $escn]
			append browser_path $escn
			_ls $this [lindex $info 1] $name
		}
		blob {
			set name [lindex $info 2]
			set p {}
			foreach n $browser_stack {
				append p [lindex $n 1]
			}
			append p $name
			blame::new $browser_commit $p {}
		}
		}
	}
}

method _click {was_double_click pos} {
	if {$browser_busy} return
	set lno [lindex [split [$w index $pos] .] 0]
	focus $w

	if {[lindex $browser_files [expr {$lno - 1}]] ne {}} {
		$w tag remove in_sel 0.0 end
		$w tag add in_sel $lno.0 [expr {$lno + 1}].0
		if {$was_double_click} {
			_enter $this
		}
	}
}

method _ls {tree_id {name {}}} {
	set ls_buf {}
	set browser_files {}
	set browser_busy 1

	$w conf -state normal
	$w tag remove in_sel 0.0 end
	$w delete 0.0 end
	if {$browser_stack ne {}} {
		$w image create end \
			-align center -padx 5 -pady 1 \
			-name icon0 \
			-image ::browser::img_parent
		$w insert end [mc "\[Up To Parent\]"]
		lappend browser_files parent
	}
	lappend browser_stack [list $tree_id $name]
	$w conf -state disabled

	set fd [git_read ls-tree -z $tree_id]
	fconfigure $fd -blocking 0 -translation binary -encoding utf-8
	fileevent $fd readable [cb _read $fd]
}

method _read {fd} {
	append ls_buf [read $fd]
	set pck [split $ls_buf "\0"]
	set ls_buf [lindex $pck end]

	set n [llength $browser_files]
	$w conf -state normal
	foreach p [lrange $pck 0 end-1] {
		set tab [string first "\t" $p]
		if {$tab == -1} continue

		set info [split [string range $p 0 [expr {$tab - 1}]] { }]
		set path [string range $p [expr {$tab + 1}] end]
		set type   [lindex $info 1]
		set object [lindex $info 2]

		switch -- $type {
		blob {
			scan [lindex $info 0] %o mode
			if {$mode == 0120000} {
				set image ::browser::img_symlink
			} elseif {($mode & 0100) != 0} {
				set image ::browser::img_xblob
			} else {
				set image ::browser::img_rblob
			}
		}
		tree {
			set image ::browser::img_tree
			append path /
		}
		default {
			set image ::browser::img_unknown
		}
		}

		if {$n > 0} {$w insert end "\n"}
		$w image create end \
			-align center -padx 5 -pady 1 \
			-name icon[incr n] \
			-image $image
		$w insert end [escape_path $path]
		lappend browser_files [list $type $object $path]
	}
	$w conf -state disabled

	if {[eof $fd]} {
		close $fd
		set browser_status [mc "Ready."]
		set browser_busy 0
		set ls_buf {}
		if {$n > 0} {
			$w tag add in_sel 1.0 2.0
			focus -force $w
		}
	}
} ifdeleted {
	catch {close $fd}
}

}

class browser_open {

field w              ; # widget path
field w_rev          ; # mega-widget to pick the initial revision

constructor dialog {} {
	global use_ttk NS
	make_dialog top w
	wm withdraw $top
	wm title $top [append "[appname] ([reponame]): " [mc "Browse Branch Files"]]
	if {$top ne {.}} {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
		wm transient $top .
	}

	${NS}::label $w.header \
		-text [mc "Browse Branch Files"] \
		-font font_uibold \
		-anchor center
	pack $w.header -side top -fill x

	${NS}::frame $w.buttons
	${NS}::button $w.buttons.browse -text [mc Browse] \
		-default active \
		-command [cb _open]
	pack $w.buttons.browse -side right
	${NS}::button $w.buttons.cancel -text [mc Cancel] \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	set w_rev [::choose_rev::new $w.rev [mc Revision]]
	$w_rev bind_listbox <Double-Button-1> [cb _open]
	pack $w.rev -anchor nw -fill both -expand 1 -pady 5 -padx 5

	bind $w <Visibility> [cb _visible]
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Key-Return> [cb _open]\;break
	wm deiconify $top
	tkwait window $w
}

method _open {} {
	if {[catch {$w_rev commit_or_die} err]} {
		return
	}
	set name [$w_rev get]
	destroy $w
	browser::new $name
}

method _visible {} {
	grab $w
	$w_rev focus_filter
}

}
