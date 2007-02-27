#!/bin/sh
# Tcl ignores the next line -*- tcl -*- \
exec wish "$0" -- "$@"

set appvers {@@GITGUI_VERSION@@}
set copyright {
Copyright © 2006, 2007 Shawn Pearce, et. al.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA}
set gitgui_credits {
Paul Mackerras
}

######################################################################
##
## read only globals

set _appname [lindex [file split $argv0] end]
set _gitdir {}
set _gitexec {}
set _reponame {}
set _iscygwin {}

proc appname {} {
	global _appname
	return $_appname
}

proc gitdir {args} {
	global _gitdir
	if {$args eq {}} {
		return $_gitdir
	}
	return [eval [concat [list file join $_gitdir] $args]]
}

proc gitexec {args} {
	global _gitexec
	if {$_gitexec eq {}} {
		if {[catch {set _gitexec [git --exec-path]} err]} {
			error "Git not installed?\n\n$err"
		}
	}
	if {$args eq {}} {
		return $_gitexec
	}
	return [eval [concat [list file join $_gitexec] $args]]
}

proc reponame {} {
	global _reponame
	return $_reponame
}

proc is_MacOSX {} {
	global tcl_platform tk_library
	if {[tk windowingsystem] eq {aqua}} {
		return 1
	}
	return 0
}

proc is_Windows {} {
	global tcl_platform
	if {$tcl_platform(platform) eq {windows}} {
		return 1
	}
	return 0
}

proc is_Cygwin {} {
	global tcl_platform _iscygwin
	if {$_iscygwin eq {}} {
		if {$tcl_platform(platform) eq {windows}} {
			if {[catch {set p [exec cygpath --windir]} err]} {
				set _iscygwin 0
			} else {
				set _iscygwin 1
			}
		} else {
			set _iscygwin 0
		}
	}
	return $_iscygwin
}

proc is_enabled {option} {
	global enabled_options
	if {[catch {set on $enabled_options($option)}]} {return 0}
	return $on
}

proc enable_option {option} {
	global enabled_options
	set enabled_options($option) 1
}

proc disable_option {option} {
	global enabled_options
	set enabled_options($option) 0
}

######################################################################
##
## config

proc is_many_config {name} {
	switch -glob -- $name {
	remote.*.fetch -
	remote.*.push
		{return 1}
	*
		{return 0}
	}
}

proc is_config_true {name} {
	global repo_config
	if {[catch {set v $repo_config($name)}]} {
		return 0
	} elseif {$v eq {true} || $v eq {1} || $v eq {yes}} {
		return 1
	} else {
		return 0
	}
}

proc load_config {include_global} {
	global repo_config global_config default_config

	array unset global_config
	if {$include_global} {
		catch {
			set fd_rc [open "| git config --global --list" r]
			while {[gets $fd_rc line] >= 0} {
				if {[regexp {^([^=]+)=(.*)$} $line line name value]} {
					if {[is_many_config $name]} {
						lappend global_config($name) $value
					} else {
						set global_config($name) $value
					}
				}
			}
			close $fd_rc
		}
	}

	array unset repo_config
	catch {
		set fd_rc [open "| git config --list" r]
		while {[gets $fd_rc line] >= 0} {
			if {[regexp {^([^=]+)=(.*)$} $line line name value]} {
				if {[is_many_config $name]} {
					lappend repo_config($name) $value
				} else {
					set repo_config($name) $value
				}
			}
		}
		close $fd_rc
	}

	foreach name [array names default_config] {
		if {[catch {set v $global_config($name)}]} {
			set global_config($name) $default_config($name)
		}
		if {[catch {set v $repo_config($name)}]} {
			set repo_config($name) $default_config($name)
		}
	}
}

proc save_config {} {
	global default_config font_descs
	global repo_config global_config
	global repo_config_new global_config_new

	foreach option $font_descs {
		set name [lindex $option 0]
		set font [lindex $option 1]
		font configure $font \
			-family $global_config_new(gui.$font^^family) \
			-size $global_config_new(gui.$font^^size)
		font configure ${font}bold \
			-family $global_config_new(gui.$font^^family) \
			-size $global_config_new(gui.$font^^size)
		set global_config_new(gui.$name) [font configure $font]
		unset global_config_new(gui.$font^^family)
		unset global_config_new(gui.$font^^size)
	}

	foreach name [array names default_config] {
		set value $global_config_new($name)
		if {$value ne $global_config($name)} {
			if {$value eq $default_config($name)} {
				catch {git config --global --unset $name}
			} else {
				regsub -all "\[{}\]" $value {"} value
				git config --global $name $value
			}
			set global_config($name) $value
			if {$value eq $repo_config($name)} {
				catch {git config --unset $name}
				set repo_config($name) $value
			}
		}
	}

	foreach name [array names default_config] {
		set value $repo_config_new($name)
		if {$value ne $repo_config($name)} {
			if {$value eq $global_config($name)} {
				catch {git config --unset $name}
			} else {
				regsub -all "\[{}\]" $value {"} value
				git config $name $value
			}
			set repo_config($name) $value
		}
	}
}

######################################################################
##
## handy utils

proc git {args} {
	return [eval exec git $args]
}

proc error_popup {msg} {
	set title [appname]
	if {[reponame] ne {}} {
		append title " ([reponame])"
	}
	set cmd [list tk_messageBox \
		-icon error \
		-type ok \
		-title "$title: error" \
		-message $msg]
	if {[winfo ismapped .]} {
		lappend cmd -parent .
	}
	eval $cmd
}

proc warn_popup {msg} {
	set title [appname]
	if {[reponame] ne {}} {
		append title " ([reponame])"
	}
	set cmd [list tk_messageBox \
		-icon warning \
		-type ok \
		-title "$title: warning" \
		-message $msg]
	if {[winfo ismapped .]} {
		lappend cmd -parent .
	}
	eval $cmd
}

proc info_popup {msg {parent .}} {
	set title [appname]
	if {[reponame] ne {}} {
		append title " ([reponame])"
	}
	tk_messageBox \
		-parent $parent \
		-icon info \
		-type ok \
		-title $title \
		-message $msg
}

proc ask_popup {msg} {
	set title [appname]
	if {[reponame] ne {}} {
		append title " ([reponame])"
	}
	return [tk_messageBox \
		-parent . \
		-icon question \
		-type yesno \
		-title $title \
		-message $msg]
}

######################################################################
##
## version check

set req_maj 1
set req_min 5

if {[catch {set v [git --version]} err]} {
	catch {wm withdraw .}
	error_popup "Cannot determine Git version:

$err

[appname] requires Git $req_maj.$req_min or later."
	exit 1
}
if {[regexp {^git version (\d+)\.(\d+)} $v _junk act_maj act_min]} {
	if {$act_maj < $req_maj
		|| ($act_maj == $req_maj && $act_min < $req_min)} {
		catch {wm withdraw .}
		error_popup "[appname] requires Git $req_maj.$req_min or later.

You are using $v."
		exit 1
	}
} else {
	catch {wm withdraw .}
	error_popup "Cannot parse Git version string:\n\n$v"
	exit 1
}
unset -nocomplain v _junk act_maj act_min req_maj req_min

######################################################################
##
## repository setup

if {   [catch {set _gitdir $env(GIT_DIR)}]
	&& [catch {set _gitdir [git rev-parse --git-dir]} err]} {
	catch {wm withdraw .}
	error_popup "Cannot find the git directory:\n\n$err"
	exit 1
}
if {![file isdirectory $_gitdir] && [is_Cygwin]} {
	catch {set _gitdir [exec cygpath --unix $_gitdir]}
}
if {![file isdirectory $_gitdir]} {
	catch {wm withdraw .}
	error_popup "Git directory not found:\n\n$_gitdir"
	exit 1
}
if {[lindex [file split $_gitdir] end] ne {.git}} {
	catch {wm withdraw .}
	error_popup "Cannot use funny .git directory:\n\n$_gitdir"
	exit 1
}
if {[catch {cd [file dirname $_gitdir]} err]} {
	catch {wm withdraw .}
	error_popup "No working directory [file dirname $_gitdir]:\n\n$err"
	exit 1
}
set _reponame [lindex [file split \
	[file normalize [file dirname $_gitdir]]] \
	end]

######################################################################
##
## global init

set current_diff_path {}
set current_diff_side {}
set diff_actions [list]
set ui_status_value {Initializing...}

set HEAD {}
set PARENT {}
set MERGE_HEAD [list]
set commit_type {}
set empty_tree {}
set current_branch {}
set current_diff_path {}
set selected_commit_type new

######################################################################
##
## task management

set rescan_active 0
set diff_active 0
set last_clicked {}

set disable_on_lock [list]
set index_lock_type none

proc lock_index {type} {
	global index_lock_type disable_on_lock

	if {$index_lock_type eq {none}} {
		set index_lock_type $type
		foreach w $disable_on_lock {
			uplevel #0 $w disabled
		}
		return 1
	} elseif {$index_lock_type eq "begin-$type"} {
		set index_lock_type $type
		return 1
	}
	return 0
}

proc unlock_index {} {
	global index_lock_type disable_on_lock

	set index_lock_type none
	foreach w $disable_on_lock {
		uplevel #0 $w normal
	}
}

######################################################################
##
## status

proc repository_state {ctvar hdvar mhvar} {
	global current_branch
	upvar $ctvar ct $hdvar hd $mhvar mh

	set mh [list]

	if {[catch {set current_branch [git symbolic-ref HEAD]}]} {
		set current_branch {}
	} else {
		regsub ^refs/((heads|tags|remotes)/)? \
			$current_branch \
			{} \
			current_branch
	}

	if {[catch {set hd [git rev-parse --verify HEAD]}]} {
		set hd {}
		set ct initial
		return
	}

	set merge_head [gitdir MERGE_HEAD]
	if {[file exists $merge_head]} {
		set ct merge
		set fd_mh [open $merge_head r]
		while {[gets $fd_mh line] >= 0} {
			lappend mh $line
		}
		close $fd_mh
		return
	}

	set ct normal
}

proc PARENT {} {
	global PARENT empty_tree

	set p [lindex $PARENT 0]
	if {$p ne {}} {
		return $p
	}
	if {$empty_tree eq {}} {
		set empty_tree [git mktree << {}]
	}
	return $empty_tree
}

proc rescan {after {honor_trustmtime 1}} {
	global HEAD PARENT MERGE_HEAD commit_type
	global ui_index ui_workdir ui_status_value ui_comm
	global rescan_active file_states
	global repo_config

	if {$rescan_active > 0 || ![lock_index read]} return

	repository_state newType newHEAD newMERGE_HEAD
	if {[string match amend* $commit_type]
		&& $newType eq {normal}
		&& $newHEAD eq $HEAD} {
	} else {
		set HEAD $newHEAD
		set PARENT $newHEAD
		set MERGE_HEAD $newMERGE_HEAD
		set commit_type $newType
	}

	array unset file_states

	if {![$ui_comm edit modified]
		|| [string trim [$ui_comm get 0.0 end]] eq {}} {
		if {[load_message GITGUI_MSG]} {
		} elseif {[load_message MERGE_MSG]} {
		} elseif {[load_message SQUASH_MSG]} {
		}
		$ui_comm edit reset
		$ui_comm edit modified false
	}

	if {[is_enabled branch]} {
		load_all_heads
		populate_branch_menu
	}

	if {$honor_trustmtime && $repo_config(gui.trustmtime) eq {true}} {
		rescan_stage2 {} $after
	} else {
		set rescan_active 1
		set ui_status_value {Refreshing file status...}
		set cmd [list git update-index]
		lappend cmd -q
		lappend cmd --unmerged
		lappend cmd --ignore-missing
		lappend cmd --refresh
		set fd_rf [open "| $cmd" r]
		fconfigure $fd_rf -blocking 0 -translation binary
		fileevent $fd_rf readable \
			[list rescan_stage2 $fd_rf $after]
	}
}

proc rescan_stage2 {fd after} {
	global ui_status_value
	global rescan_active buf_rdi buf_rdf buf_rlo

	if {$fd ne {}} {
		read $fd
		if {![eof $fd]} return
		close $fd
	}

	set ls_others [list | git ls-files --others -z \
		--exclude-per-directory=.gitignore]
	set info_exclude [gitdir info exclude]
	if {[file readable $info_exclude]} {
		lappend ls_others "--exclude-from=$info_exclude"
	}

	set buf_rdi {}
	set buf_rdf {}
	set buf_rlo {}

	set rescan_active 3
	set ui_status_value {Scanning for modified files ...}
	set fd_di [open "| git diff-index --cached -z [PARENT]" r]
	set fd_df [open "| git diff-files -z" r]
	set fd_lo [open $ls_others r]

	fconfigure $fd_di -blocking 0 -translation binary -encoding binary
	fconfigure $fd_df -blocking 0 -translation binary -encoding binary
	fconfigure $fd_lo -blocking 0 -translation binary -encoding binary
	fileevent $fd_di readable [list read_diff_index $fd_di $after]
	fileevent $fd_df readable [list read_diff_files $fd_df $after]
	fileevent $fd_lo readable [list read_ls_others $fd_lo $after]
}

proc load_message {file} {
	global ui_comm

	set f [gitdir $file]
	if {[file isfile $f]} {
		if {[catch {set fd [open $f r]}]} {
			return 0
		}
		set content [string trim [read $fd]]
		close $fd
		regsub -all -line {[ \r\t]+$} $content {} content
		$ui_comm delete 0.0 end
		$ui_comm insert end $content
		return 1
	}
	return 0
}

proc read_diff_index {fd after} {
	global buf_rdi

	append buf_rdi [read $fd]
	set c 0
	set n [string length $buf_rdi]
	while {$c < $n} {
		set z1 [string first "\0" $buf_rdi $c]
		if {$z1 == -1} break
		incr z1
		set z2 [string first "\0" $buf_rdi $z1]
		if {$z2 == -1} break

		incr c
		set i [split [string range $buf_rdi $c [expr {$z1 - 2}]] { }]
		set p [string range $buf_rdi $z1 [expr {$z2 - 1}]]
		merge_state \
			[encoding convertfrom $p] \
			[lindex $i 4]? \
			[list [lindex $i 0] [lindex $i 2]] \
			[list]
		set c $z2
		incr c
	}
	if {$c < $n} {
		set buf_rdi [string range $buf_rdi $c end]
	} else {
		set buf_rdi {}
	}

	rescan_done $fd buf_rdi $after
}

proc read_diff_files {fd after} {
	global buf_rdf

	append buf_rdf [read $fd]
	set c 0
	set n [string length $buf_rdf]
	while {$c < $n} {
		set z1 [string first "\0" $buf_rdf $c]
		if {$z1 == -1} break
		incr z1
		set z2 [string first "\0" $buf_rdf $z1]
		if {$z2 == -1} break

		incr c
		set i [split [string range $buf_rdf $c [expr {$z1 - 2}]] { }]
		set p [string range $buf_rdf $z1 [expr {$z2 - 1}]]
		merge_state \
			[encoding convertfrom $p] \
			?[lindex $i 4] \
			[list] \
			[list [lindex $i 0] [lindex $i 2]]
		set c $z2
		incr c
	}
	if {$c < $n} {
		set buf_rdf [string range $buf_rdf $c end]
	} else {
		set buf_rdf {}
	}

	rescan_done $fd buf_rdf $after
}

proc read_ls_others {fd after} {
	global buf_rlo

	append buf_rlo [read $fd]
	set pck [split $buf_rlo "\0"]
	set buf_rlo [lindex $pck end]
	foreach p [lrange $pck 0 end-1] {
		merge_state [encoding convertfrom $p] ?O
	}
	rescan_done $fd buf_rlo $after
}

proc rescan_done {fd buf after} {
	global rescan_active
	global file_states repo_config
	upvar $buf to_clear

	if {![eof $fd]} return
	set to_clear {}
	close $fd
	if {[incr rescan_active -1] > 0} return

	prune_selection
	unlock_index
	display_all_files
	reshow_diff
	uplevel #0 $after
}

proc prune_selection {} {
	global file_states selected_paths

	foreach path [array names selected_paths] {
		if {[catch {set still_here $file_states($path)}]} {
			unset selected_paths($path)
		}
	}
}

######################################################################
##
## diff

proc clear_diff {} {
	global ui_diff current_diff_path current_diff_header
	global ui_index ui_workdir

	$ui_diff conf -state normal
	$ui_diff delete 0.0 end
	$ui_diff conf -state disabled

	set current_diff_path {}
	set current_diff_header {}

	$ui_index tag remove in_diff 0.0 end
	$ui_workdir tag remove in_diff 0.0 end
}

proc reshow_diff {} {
	global ui_status_value file_states file_lists
	global current_diff_path current_diff_side

	set p $current_diff_path
	if {$p eq {}} {
		# No diff is being shown.
	} elseif {$current_diff_side eq {}
		|| [catch {set s $file_states($p)}]
		|| [lsearch -sorted -exact $file_lists($current_diff_side) $p] == -1} {
		clear_diff
	} else {
		show_diff $p $current_diff_side
	}
}

proc handle_empty_diff {} {
	global current_diff_path file_states file_lists

	set path $current_diff_path
	set s $file_states($path)
	if {[lindex $s 0] ne {_M}} return

	info_popup "No differences detected.

[short_path $path] has no changes.

The modification date of this file was updated
by another application, but the content within
the file was not changed.

A rescan will be automatically started to find
other files which may have the same state."

	clear_diff
	display_file $path __
	rescan {set ui_status_value {Ready.}} 0
}

proc show_diff {path w {lno {}}} {
	global file_states file_lists
	global is_3way_diff diff_active repo_config
	global ui_diff ui_status_value ui_index ui_workdir
	global current_diff_path current_diff_side current_diff_header

	if {$diff_active || ![lock_index read]} return

	clear_diff
	if {$lno == {}} {
		set lno [lsearch -sorted -exact $file_lists($w) $path]
		if {$lno >= 0} {
			incr lno
		}
	}
	if {$lno >= 1} {
		$w tag add in_diff $lno.0 [expr {$lno + 1}].0
	}

	set s $file_states($path)
	set m [lindex $s 0]
	set is_3way_diff 0
	set diff_active 1
	set current_diff_path $path
	set current_diff_side $w
	set current_diff_header {}
	set ui_status_value "Loading diff of [escape_path $path]..."

	# - Git won't give us the diff, there's nothing to compare to!
	#
	if {$m eq {_O}} {
		set max_sz [expr {128 * 1024}]
		if {[catch {
				set fd [open $path r]
				set content [read $fd $max_sz]
				close $fd
				set sz [file size $path]
			} err ]} {
			set diff_active 0
			unlock_index
			set ui_status_value "Unable to display [escape_path $path]"
			error_popup "Error loading file:\n\n$err"
			return
		}
		$ui_diff conf -state normal
		if {![catch {set type [exec file $path]}]} {
			set n [string length $path]
			if {[string equal -length $n $path $type]} {
				set type [string range $type $n end]
				regsub {^:?\s*} $type {} type
			}
			$ui_diff insert end "* $type\n" d_@
		}
		if {[string first "\0" $content] != -1} {
			$ui_diff insert end \
				"* Binary file (not showing content)." \
				d_@
		} else {
			if {$sz > $max_sz} {
				$ui_diff insert end \
"* Untracked file is $sz bytes.
* Showing only first $max_sz bytes.
" d_@
			}
			$ui_diff insert end $content
			if {$sz > $max_sz} {
				$ui_diff insert end "
* Untracked file clipped here by [appname].
* To see the entire file, use an external editor.
" d_@
			}
		}
		$ui_diff conf -state disabled
		set diff_active 0
		unlock_index
		set ui_status_value {Ready.}
		return
	}

	set cmd [list | git]
	if {$w eq $ui_index} {
		lappend cmd diff-index
		lappend cmd --cached
	} elseif {$w eq $ui_workdir} {
		if {[string index $m 0] eq {U}} {
			lappend cmd diff
		} else {
			lappend cmd diff-files
		}
	}

	lappend cmd -p
	lappend cmd --no-color
	if {$repo_config(gui.diffcontext) > 0} {
		lappend cmd "-U$repo_config(gui.diffcontext)"
	}
	if {$w eq $ui_index} {
		lappend cmd [PARENT]
	}
	lappend cmd --
	lappend cmd $path

	if {[catch {set fd [open $cmd r]} err]} {
		set diff_active 0
		unlock_index
		set ui_status_value "Unable to display [escape_path $path]"
		error_popup "Error loading diff:\n\n$err"
		return
	}

	fconfigure $fd \
		-blocking 0 \
		-encoding binary \
		-translation binary
	fileevent $fd readable [list read_diff $fd]
}

proc read_diff {fd} {
	global ui_diff ui_status_value diff_active
	global is_3way_diff current_diff_header

	$ui_diff conf -state normal
	while {[gets $fd line] >= 0} {
		# -- Cleanup uninteresting diff header lines.
		#
		if {   [string match {diff --git *}      $line]
			|| [string match {diff --cc *}       $line]
			|| [string match {diff --combined *} $line]
			|| [string match {--- *}             $line]
			|| [string match {+++ *}             $line]} {
			append current_diff_header $line "\n"
			continue
		}
		if {[string match {index *} $line]} continue
		if {$line eq {deleted file mode 120000}} {
			set line "deleted symlink"
		}

		# -- Automatically detect if this is a 3 way diff.
		#
		if {[string match {@@@ *} $line]} {set is_3way_diff 1}

		if {[string match {mode *} $line]
			|| [string match {new file *} $line]
			|| [string match {deleted file *} $line]
			|| [string match {Binary files * and * differ} $line]
			|| $line eq {\ No newline at end of file}
			|| [regexp {^\* Unmerged path } $line]} {
			set tags {}
		} elseif {$is_3way_diff} {
			set op [string range $line 0 1]
			switch -- $op {
			{  } {set tags {}}
			{@@} {set tags d_@}
			{ +} {set tags d_s+}
			{ -} {set tags d_s-}
			{+ } {set tags d_+s}
			{- } {set tags d_-s}
			{--} {set tags d_--}
			{++} {
				if {[regexp {^\+\+([<>]{7} |={7})} $line _g op]} {
					set line [string replace $line 0 1 {  }]
					set tags d$op
				} else {
					set tags d_++
				}
			}
			default {
				puts "error: Unhandled 3 way diff marker: {$op}"
				set tags {}
			}
			}
		} else {
			set op [string index $line 0]
			switch -- $op {
			{ } {set tags {}}
			{@} {set tags d_@}
			{-} {set tags d_-}
			{+} {
				if {[regexp {^\+([<>]{7} |={7})} $line _g op]} {
					set line [string replace $line 0 0 { }]
					set tags d$op
				} else {
					set tags d_+
				}
			}
			default {
				puts "error: Unhandled 2 way diff marker: {$op}"
				set tags {}
			}
			}
		}
		$ui_diff insert end $line $tags
		if {[string index $line end] eq "\r"} {
			$ui_diff tag add d_cr {end - 2c}
		}
		$ui_diff insert end "\n" $tags
	}
	$ui_diff conf -state disabled

	if {[eof $fd]} {
		close $fd
		set diff_active 0
		unlock_index
		set ui_status_value {Ready.}

		if {[$ui_diff index end] eq {2.0}} {
			handle_empty_diff
		}
	}
}

proc apply_hunk {x y} {
	global current_diff_path current_diff_header current_diff_side
	global ui_diff ui_index file_states

	if {$current_diff_path eq {} || $current_diff_header eq {}} return
	if {![lock_index apply_hunk]} return

	set apply_cmd {git apply --cached --whitespace=nowarn}
	set mi [lindex $file_states($current_diff_path) 0]
	if {$current_diff_side eq $ui_index} {
		set mode unstage
		lappend apply_cmd --reverse
		if {[string index $mi 0] ne {M}} {
			unlock_index
			return
		}
	} else {
		set mode stage
		if {[string index $mi 1] ne {M}} {
			unlock_index
			return
		}
	}

	set s_lno [lindex [split [$ui_diff index @$x,$y] .] 0]
	set s_lno [$ui_diff search -backwards -regexp ^@@ $s_lno.0 0.0]
	if {$s_lno eq {}} {
		unlock_index
		return
	}

	set e_lno [$ui_diff search -forwards -regexp ^@@ "$s_lno + 1 lines" end]
	if {$e_lno eq {}} {
		set e_lno end
	}

	if {[catch {
		set p [open "| $apply_cmd" w]
		fconfigure $p -translation binary -encoding binary
		puts -nonewline $p $current_diff_header
		puts -nonewline $p [$ui_diff get $s_lno $e_lno]
		close $p} err]} {
		error_popup "Failed to $mode selected hunk.\n\n$err"
		unlock_index
		return
	}

	$ui_diff conf -state normal
	$ui_diff delete $s_lno $e_lno
	$ui_diff conf -state disabled

	if {[$ui_diff get 1.0 end] eq "\n"} {
		set o _
	} else {
		set o ?
	}

	if {$current_diff_side eq $ui_index} {
		set mi ${o}M
	} elseif {[string index $mi 0] eq {_}} {
		set mi M$o
	} else {
		set mi ?$o
	}
	unlock_index
	display_file $current_diff_path $mi
	if {$o eq {_}} {
		clear_diff
	}
}

######################################################################
##
## commit

proc load_last_commit {} {
	global HEAD PARENT MERGE_HEAD commit_type ui_comm
	global repo_config

	if {[llength $PARENT] == 0} {
		error_popup {There is nothing to amend.

You are about to create the initial commit.
There is no commit before this to amend.
}
		return
	}

	repository_state curType curHEAD curMERGE_HEAD
	if {$curType eq {merge}} {
		error_popup {Cannot amend while merging.

You are currently in the middle of a merge that
has not been fully completed.  You cannot amend
the prior commit unless you first abort the
current merge activity.
}
		return
	}

	set msg {}
	set parents [list]
	if {[catch {
			set fd [open "| git cat-file commit $curHEAD" r]
			fconfigure $fd -encoding binary -translation lf
			if {[catch {set enc $repo_config(i18n.commitencoding)}]} {
				set enc utf-8
			}
			while {[gets $fd line] > 0} {
				if {[string match {parent *} $line]} {
					lappend parents [string range $line 7 end]
				} elseif {[string match {encoding *} $line]} {
					set enc [string tolower [string range $line 9 end]]
				}
			}
			fconfigure $fd -encoding $enc
			set msg [string trim [read $fd]]
			close $fd
		} err]} {
		error_popup "Error loading commit data for amend:\n\n$err"
		return
	}

	set HEAD $curHEAD
	set PARENT $parents
	set MERGE_HEAD [list]
	switch -- [llength $parents] {
	0       {set commit_type amend-initial}
	1       {set commit_type amend}
	default {set commit_type amend-merge}
	}

	$ui_comm delete 0.0 end
	$ui_comm insert end $msg
	$ui_comm edit reset
	$ui_comm edit modified false
	rescan {set ui_status_value {Ready.}}
}

proc create_new_commit {} {
	global commit_type ui_comm

	set commit_type normal
	$ui_comm delete 0.0 end
	$ui_comm edit reset
	$ui_comm edit modified false
	rescan {set ui_status_value {Ready.}}
}

set GIT_COMMITTER_IDENT {}

proc committer_ident {} {
	global GIT_COMMITTER_IDENT

	if {$GIT_COMMITTER_IDENT eq {}} {
		if {[catch {set me [git var GIT_COMMITTER_IDENT]} err]} {
			error_popup "Unable to obtain your identity:\n\n$err"
			return {}
		}
		if {![regexp {^(.*) [0-9]+ [-+0-9]+$} \
			$me me GIT_COMMITTER_IDENT]} {
			error_popup "Invalid GIT_COMMITTER_IDENT:\n\n$me"
			return {}
		}
	}

	return $GIT_COMMITTER_IDENT
}

proc commit_tree {} {
	global HEAD commit_type file_states ui_comm repo_config
	global ui_status_value pch_error

	if {[committer_ident] eq {}} return
	if {![lock_index update]} return

	# -- Our in memory state should match the repository.
	#
	repository_state curType curHEAD curMERGE_HEAD
	if {[string match amend* $commit_type]
		&& $curType eq {normal}
		&& $curHEAD eq $HEAD} {
	} elseif {$commit_type ne $curType || $HEAD ne $curHEAD} {
		info_popup {Last scanned state does not match repository state.

Another Git program has modified this repository
since the last scan.  A rescan must be performed
before another commit can be created.

The rescan will be automatically started now.
}
		unlock_index
		rescan {set ui_status_value {Ready.}}
		return
	}

	# -- At least one file should differ in the index.
	#
	set files_ready 0
	foreach path [array names file_states] {
		switch -glob -- [lindex $file_states($path) 0] {
		_? {continue}
		A? -
		D? -
		M? {set files_ready 1}
		U? {
			error_popup "Unmerged files cannot be committed.

File [short_path $path] has merge conflicts.
You must resolve them and add the file before committing.
"
			unlock_index
			return
		}
		default {
			error_popup "Unknown file state [lindex $s 0] detected.

File [short_path $path] cannot be committed by this program.
"
		}
		}
	}
	if {!$files_ready} {
		info_popup {No changes to commit.

You must add at least 1 file before you can commit.
}
		unlock_index
		return
	}

	# -- A message is required.
	#
	set msg [string trim [$ui_comm get 1.0 end]]
	regsub -all -line {[ \t\r]+$} $msg {} msg
	if {$msg eq {}} {
		error_popup {Please supply a commit message.

A good commit message has the following format:

- First line: Describe in one sentance what you did.
- Second line: Blank
- Remaining lines: Describe why this change is good.
}
		unlock_index
		return
	}

	# -- Run the pre-commit hook.
	#
	set pchook [gitdir hooks pre-commit]

	# On Cygwin [file executable] might lie so we need to ask
	# the shell if the hook is executable.  Yes that's annoying.
	#
	if {[is_Cygwin] && [file isfile $pchook]} {
		set pchook [list sh -c [concat \
			"if test -x \"$pchook\";" \
			"then exec \"$pchook\" 2>&1;" \
			"fi"]]
	} elseif {[file executable $pchook]} {
		set pchook [list $pchook |& cat]
	} else {
		commit_writetree $curHEAD $msg
		return
	}

	set ui_status_value {Calling pre-commit hook...}
	set pch_error {}
	set fd_ph [open "| $pchook" r]
	fconfigure $fd_ph -blocking 0 -translation binary
	fileevent $fd_ph readable \
		[list commit_prehook_wait $fd_ph $curHEAD $msg]
}

proc commit_prehook_wait {fd_ph curHEAD msg} {
	global pch_error ui_status_value

	append pch_error [read $fd_ph]
	fconfigure $fd_ph -blocking 1
	if {[eof $fd_ph]} {
		if {[catch {close $fd_ph}]} {
			set ui_status_value {Commit declined by pre-commit hook.}
			hook_failed_popup pre-commit $pch_error
			unlock_index
		} else {
			commit_writetree $curHEAD $msg
		}
		set pch_error {}
		return
	}
	fconfigure $fd_ph -blocking 0
}

proc commit_writetree {curHEAD msg} {
	global ui_status_value

	set ui_status_value {Committing changes...}
	set fd_wt [open "| git write-tree" r]
	fileevent $fd_wt readable \
		[list commit_committree $fd_wt $curHEAD $msg]
}

proc commit_committree {fd_wt curHEAD msg} {
	global HEAD PARENT MERGE_HEAD commit_type
	global all_heads current_branch
	global ui_status_value ui_comm selected_commit_type
	global file_states selected_paths rescan_active
	global repo_config

	gets $fd_wt tree_id
	if {$tree_id eq {} || [catch {close $fd_wt} err]} {
		error_popup "write-tree failed:\n\n$err"
		set ui_status_value {Commit failed.}
		unlock_index
		return
	}

	# -- Build the message.
	#
	set msg_p [gitdir COMMIT_EDITMSG]
	set msg_wt [open $msg_p w]
	if {[catch {set enc $repo_config(i18n.commitencoding)}]} {
		set enc utf-8
	}
	fconfigure $msg_wt -encoding $enc -translation binary
	puts -nonewline $msg_wt $msg
	close $msg_wt

	# -- Create the commit.
	#
	set cmd [list git commit-tree $tree_id]
	set parents [concat $PARENT $MERGE_HEAD]
	if {[llength $parents] > 0} {
		foreach p $parents {
			lappend cmd -p $p
		}
	} else {
		# git commit-tree writes to stderr during initial commit.
		lappend cmd 2>/dev/null
	}
	lappend cmd <$msg_p
	if {[catch {set cmt_id [eval exec $cmd]} err]} {
		error_popup "commit-tree failed:\n\n$err"
		set ui_status_value {Commit failed.}
		unlock_index
		return
	}

	# -- Update the HEAD ref.
	#
	set reflogm commit
	if {$commit_type ne {normal}} {
		append reflogm " ($commit_type)"
	}
	set i [string first "\n" $msg]
	if {$i >= 0} {
		append reflogm {: } [string range $msg 0 [expr {$i - 1}]]
	} else {
		append reflogm {: } $msg
	}
	set cmd [list git update-ref -m $reflogm HEAD $cmt_id $curHEAD]
	if {[catch {eval exec $cmd} err]} {
		error_popup "update-ref failed:\n\n$err"
		set ui_status_value {Commit failed.}
		unlock_index
		return
	}

	# -- Cleanup after ourselves.
	#
	catch {file delete $msg_p}
	catch {file delete [gitdir MERGE_HEAD]}
	catch {file delete [gitdir MERGE_MSG]}
	catch {file delete [gitdir SQUASH_MSG]}
	catch {file delete [gitdir GITGUI_MSG]}

	# -- Let rerere do its thing.
	#
	if {[file isdirectory [gitdir rr-cache]]} {
		catch {git rerere}
	}

	# -- Run the post-commit hook.
	#
	set pchook [gitdir hooks post-commit]
	if {[is_Cygwin] && [file isfile $pchook]} {
		set pchook [list sh -c [concat \
			"if test -x \"$pchook\";" \
			"then exec \"$pchook\";" \
			"fi"]]
	} elseif {![file executable $pchook]} {
		set pchook {}
	}
	if {$pchook ne {}} {
		catch {exec $pchook &}
	}

	$ui_comm delete 0.0 end
	$ui_comm edit reset
	$ui_comm edit modified false

	if {[is_enabled singlecommit]} do_quit

	# -- Make sure our current branch exists.
	#
	if {$commit_type eq {initial}} {
		lappend all_heads $current_branch
		set all_heads [lsort -unique $all_heads]
		populate_branch_menu
	}

	# -- Update in memory status
	#
	set selected_commit_type new
	set commit_type normal
	set HEAD $cmt_id
	set PARENT $cmt_id
	set MERGE_HEAD [list]

	foreach path [array names file_states] {
		set s $file_states($path)
		set m [lindex $s 0]
		switch -glob -- $m {
		_O -
		_M -
		_D {continue}
		__ -
		A_ -
		M_ -
		D_ {
			unset file_states($path)
			catch {unset selected_paths($path)}
		}
		DO {
			set file_states($path) [list _O [lindex $s 1] {} {}]
		}
		AM -
		AD -
		MM -
		MD {
			set file_states($path) [list \
				_[string index $m 1] \
				[lindex $s 1] \
				[lindex $s 3] \
				{}]
		}
		}
	}

	display_all_files
	unlock_index
	reshow_diff
	set ui_status_value \
		"Changes committed as [string range $cmt_id 0 7]."
}

######################################################################
##
## fetch push

proc fetch_from {remote} {
	set w [new_console \
		"fetch $remote" \
		"Fetching new changes from $remote"]
	set cmd [list git fetch]
	lappend cmd $remote
	console_exec $w $cmd console_done
}

proc push_to {remote} {
	set w [new_console \
		"push $remote" \
		"Pushing changes to $remote"]
	set cmd [list git push]
	lappend cmd -v
	lappend cmd $remote
	console_exec $w $cmd console_done
}

######################################################################
##
## ui helpers

proc mapicon {w state path} {
	global all_icons

	if {[catch {set r $all_icons($state$w)}]} {
		puts "error: no icon for $w state={$state} $path"
		return file_plain
	}
	return $r
}

proc mapdesc {state path} {
	global all_descs

	if {[catch {set r $all_descs($state)}]} {
		puts "error: no desc for state={$state} $path"
		return $state
	}
	return $r
}

proc escape_path {path} {
	regsub -all {\\} $path "\\\\" path
	regsub -all "\n" $path "\\n" path
	return $path
}

proc short_path {path} {
	return [escape_path [lindex [file split $path] end]]
}

set next_icon_id 0
set null_sha1 [string repeat 0 40]

proc merge_state {path new_state {head_info {}} {index_info {}}} {
	global file_states next_icon_id null_sha1

	set s0 [string index $new_state 0]
	set s1 [string index $new_state 1]

	if {[catch {set info $file_states($path)}]} {
		set state __
		set icon n[incr next_icon_id]
	} else {
		set state [lindex $info 0]
		set icon [lindex $info 1]
		if {$head_info eq {}}  {set head_info  [lindex $info 2]}
		if {$index_info eq {}} {set index_info [lindex $info 3]}
	}

	if     {$s0 eq {?}} {set s0 [string index $state 0]} \
	elseif {$s0 eq {_}} {set s0 _}

	if     {$s1 eq {?}} {set s1 [string index $state 1]} \
	elseif {$s1 eq {_}} {set s1 _}

	if {$s0 eq {A} && $s1 eq {_} && $head_info eq {}} {
		set head_info [list 0 $null_sha1]
	} elseif {$s0 ne {_} && [string index $state 0] eq {_}
		&& $head_info eq {}} {
		set head_info $index_info
	}

	set file_states($path) [list $s0$s1 $icon \
		$head_info $index_info \
		]
	return $state
}

proc display_file_helper {w path icon_name old_m new_m} {
	global file_lists

	if {$new_m eq {_}} {
		set lno [lsearch -sorted -exact $file_lists($w) $path]
		if {$lno >= 0} {
			set file_lists($w) [lreplace $file_lists($w) $lno $lno]
			incr lno
			$w conf -state normal
			$w delete $lno.0 [expr {$lno + 1}].0
			$w conf -state disabled
		}
	} elseif {$old_m eq {_} && $new_m ne {_}} {
		lappend file_lists($w) $path
		set file_lists($w) [lsort -unique $file_lists($w)]
		set lno [lsearch -sorted -exact $file_lists($w) $path]
		incr lno
		$w conf -state normal
		$w image create $lno.0 \
			-align center -padx 5 -pady 1 \
			-name $icon_name \
			-image [mapicon $w $new_m $path]
		$w insert $lno.1 "[escape_path $path]\n"
		$w conf -state disabled
	} elseif {$old_m ne $new_m} {
		$w conf -state normal
		$w image conf $icon_name -image [mapicon $w $new_m $path]
		$w conf -state disabled
	}
}

proc display_file {path state} {
	global file_states selected_paths
	global ui_index ui_workdir

	set old_m [merge_state $path $state]
	set s $file_states($path)
	set new_m [lindex $s 0]
	set icon_name [lindex $s 1]

	set o [string index $old_m 0]
	set n [string index $new_m 0]
	if {$o eq {U}} {
		set o _
	}
	if {$n eq {U}} {
		set n _
	}
	display_file_helper	$ui_index $path $icon_name $o $n

	if {[string index $old_m 0] eq {U}} {
		set o U
	} else {
		set o [string index $old_m 1]
	}
	if {[string index $new_m 0] eq {U}} {
		set n U
	} else {
		set n [string index $new_m 1]
	}
	display_file_helper	$ui_workdir $path $icon_name $o $n

	if {$new_m eq {__}} {
		unset file_states($path)
		catch {unset selected_paths($path)}
	}
}

proc display_all_files_helper {w path icon_name m} {
	global file_lists

	lappend file_lists($w) $path
	set lno [expr {[lindex [split [$w index end] .] 0] - 1}]
	$w image create end \
		-align center -padx 5 -pady 1 \
		-name $icon_name \
		-image [mapicon $w $m $path]
	$w insert end "[escape_path $path]\n"
}

proc display_all_files {} {
	global ui_index ui_workdir
	global file_states file_lists
	global last_clicked

	$ui_index conf -state normal
	$ui_workdir conf -state normal

	$ui_index delete 0.0 end
	$ui_workdir delete 0.0 end
	set last_clicked {}

	set file_lists($ui_index) [list]
	set file_lists($ui_workdir) [list]

	foreach path [lsort [array names file_states]] {
		set s $file_states($path)
		set m [lindex $s 0]
		set icon_name [lindex $s 1]

		set s [string index $m 0]
		if {$s ne {U} && $s ne {_}} {
			display_all_files_helper $ui_index $path \
				$icon_name $s
		}

		if {[string index $m 0] eq {U}} {
			set s U
		} else {
			set s [string index $m 1]
		}
		if {$s ne {_}} {
			display_all_files_helper $ui_workdir $path \
				$icon_name $s
		}
	}

	$ui_index conf -state disabled
	$ui_workdir conf -state disabled
}

proc update_indexinfo {msg pathList after} {
	global update_index_cp ui_status_value

	if {![lock_index update]} return

	set update_index_cp 0
	set pathList [lsort $pathList]
	set totalCnt [llength $pathList]
	set batch [expr {int($totalCnt * .01) + 1}]
	if {$batch > 25} {set batch 25}

	set ui_status_value [format \
		"$msg... %i/%i files (%.2f%%)" \
		$update_index_cp \
		$totalCnt \
		0.0]
	set fd [open "| git update-index -z --index-info" w]
	fconfigure $fd \
		-blocking 0 \
		-buffering full \
		-buffersize 512 \
		-encoding binary \
		-translation binary
	fileevent $fd writable [list \
		write_update_indexinfo \
		$fd \
		$pathList \
		$totalCnt \
		$batch \
		$msg \
		$after \
		]
}

proc write_update_indexinfo {fd pathList totalCnt batch msg after} {
	global update_index_cp ui_status_value
	global file_states current_diff_path

	if {$update_index_cp >= $totalCnt} {
		close $fd
		unlock_index
		uplevel #0 $after
		return
	}

	for {set i $batch} \
		{$update_index_cp < $totalCnt && $i > 0} \
		{incr i -1} {
		set path [lindex $pathList $update_index_cp]
		incr update_index_cp

		set s $file_states($path)
		switch -glob -- [lindex $s 0] {
		A? {set new _O}
		M? {set new _M}
		D_ {set new _D}
		D? {set new _?}
		?? {continue}
		}
		set info [lindex $s 2]
		if {$info eq {}} continue

		puts -nonewline $fd "$info\t[encoding convertto $path]\0"
		display_file $path $new
	}

	set ui_status_value [format \
		"$msg... %i/%i files (%.2f%%)" \
		$update_index_cp \
		$totalCnt \
		[expr {100.0 * $update_index_cp / $totalCnt}]]
}

proc update_index {msg pathList after} {
	global update_index_cp ui_status_value

	if {![lock_index update]} return

	set update_index_cp 0
	set pathList [lsort $pathList]
	set totalCnt [llength $pathList]
	set batch [expr {int($totalCnt * .01) + 1}]
	if {$batch > 25} {set batch 25}

	set ui_status_value [format \
		"$msg... %i/%i files (%.2f%%)" \
		$update_index_cp \
		$totalCnt \
		0.0]
	set fd [open "| git update-index --add --remove -z --stdin" w]
	fconfigure $fd \
		-blocking 0 \
		-buffering full \
		-buffersize 512 \
		-encoding binary \
		-translation binary
	fileevent $fd writable [list \
		write_update_index \
		$fd \
		$pathList \
		$totalCnt \
		$batch \
		$msg \
		$after \
		]
}

proc write_update_index {fd pathList totalCnt batch msg after} {
	global update_index_cp ui_status_value
	global file_states current_diff_path

	if {$update_index_cp >= $totalCnt} {
		close $fd
		unlock_index
		uplevel #0 $after
		return
	}

	for {set i $batch} \
		{$update_index_cp < $totalCnt && $i > 0} \
		{incr i -1} {
		set path [lindex $pathList $update_index_cp]
		incr update_index_cp

		switch -glob -- [lindex $file_states($path) 0] {
		AD {set new __}
		?D {set new D_}
		_O -
		AM {set new A_}
		U? {
			if {[file exists $path]} {
				set new M_
			} else {
				set new D_
			}
		}
		?M {set new M_}
		?? {continue}
		}
		puts -nonewline $fd "[encoding convertto $path]\0"
		display_file $path $new
	}

	set ui_status_value [format \
		"$msg... %i/%i files (%.2f%%)" \
		$update_index_cp \
		$totalCnt \
		[expr {100.0 * $update_index_cp / $totalCnt}]]
}

proc checkout_index {msg pathList after} {
	global update_index_cp ui_status_value

	if {![lock_index update]} return

	set update_index_cp 0
	set pathList [lsort $pathList]
	set totalCnt [llength $pathList]
	set batch [expr {int($totalCnt * .01) + 1}]
	if {$batch > 25} {set batch 25}

	set ui_status_value [format \
		"$msg... %i/%i files (%.2f%%)" \
		$update_index_cp \
		$totalCnt \
		0.0]
	set cmd [list git checkout-index]
	lappend cmd --index
	lappend cmd --quiet
	lappend cmd --force
	lappend cmd -z
	lappend cmd --stdin
	set fd [open "| $cmd " w]
	fconfigure $fd \
		-blocking 0 \
		-buffering full \
		-buffersize 512 \
		-encoding binary \
		-translation binary
	fileevent $fd writable [list \
		write_checkout_index \
		$fd \
		$pathList \
		$totalCnt \
		$batch \
		$msg \
		$after \
		]
}

proc write_checkout_index {fd pathList totalCnt batch msg after} {
	global update_index_cp ui_status_value
	global file_states current_diff_path

	if {$update_index_cp >= $totalCnt} {
		close $fd
		unlock_index
		uplevel #0 $after
		return
	}

	for {set i $batch} \
		{$update_index_cp < $totalCnt && $i > 0} \
		{incr i -1} {
		set path [lindex $pathList $update_index_cp]
		incr update_index_cp
		switch -glob -- [lindex $file_states($path) 0] {
		U? {continue}
		?M -
		?D {
			puts -nonewline $fd "[encoding convertto $path]\0"
			display_file $path ?_
		}
		}
	}

	set ui_status_value [format \
		"$msg... %i/%i files (%.2f%%)" \
		$update_index_cp \
		$totalCnt \
		[expr {100.0 * $update_index_cp / $totalCnt}]]
}

######################################################################
##
## branch management

proc is_tracking_branch {name} {
	global tracking_branches

	if {![catch {set info $tracking_branches($name)}]} {
		return 1
	}
	foreach t [array names tracking_branches] {
		if {[string match {*/\*} $t] && [string match $t $name]} {
			return 1
		}
	}
	return 0
}

proc load_all_heads {} {
	global all_heads

	set all_heads [list]
	set fd [open "| git for-each-ref --format=%(refname) refs/heads" r]
	while {[gets $fd line] > 0} {
		if {[is_tracking_branch $line]} continue
		if {![regsub ^refs/heads/ $line {} name]} continue
		lappend all_heads $name
	}
	close $fd

	set all_heads [lsort $all_heads]
}

proc populate_branch_menu {} {
	global all_heads disable_on_lock

	set m .mbar.branch
	set last [$m index last]
	for {set i 0} {$i <= $last} {incr i} {
		if {[$m type $i] eq {separator}} {
			$m delete $i last
			set new_dol [list]
			foreach a $disable_on_lock {
				if {[lindex $a 0] ne $m || [lindex $a 2] < $i} {
					lappend new_dol $a
				}
			}
			set disable_on_lock $new_dol
			break
		}
	}

	if {$all_heads ne {}} {
		$m add separator
	}
	foreach b $all_heads {
		$m add radiobutton \
			-label $b \
			-command [list switch_branch $b] \
			-variable current_branch \
			-value $b \
			-font font_ui
		lappend disable_on_lock \
			[list $m entryconf [$m index last] -state]
	}
}

proc all_tracking_branches {} {
	global tracking_branches

	set all_trackings {}
	set cmd {}
	foreach name [array names tracking_branches] {
		if {[regsub {/\*$} $name {} name]} {
			lappend cmd $name
		} else {
			regsub ^refs/(heads|remotes)/ $name {} name
			lappend all_trackings $name
		}
	}

	if {$cmd ne {}} {
		set fd [open "| git for-each-ref --format=%(refname) $cmd" r]
		while {[gets $fd name] > 0} {
			regsub ^refs/(heads|remotes)/ $name {} name
			lappend all_trackings $name
		}
		close $fd
	}

	return [lsort -unique $all_trackings]
}

proc load_all_tags {} {
	set all_tags [list]
	set fd [open "| git for-each-ref --format=%(refname) refs/tags" r]
	while {[gets $fd line] > 0} {
		if {![regsub ^refs/tags/ $line {} name]} continue
		lappend all_tags $name
	}
	close $fd

	return [lsort $all_tags]
}

proc do_create_branch_action {w} {
	global all_heads null_sha1 repo_config
	global create_branch_checkout create_branch_revtype
	global create_branch_head create_branch_trackinghead
	global create_branch_name create_branch_revexp
	global create_branch_tag

	set newbranch $create_branch_name
	if {$newbranch eq {}
		|| $newbranch eq $repo_config(gui.newbranchtemplate)} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Please supply a branch name."
		focus $w.desc.name_t
		return
	}
	if {![catch {git show-ref --verify -- "refs/heads/$newbranch"}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Branch '$newbranch' already exists."
		focus $w.desc.name_t
		return
	}
	if {[catch {git check-ref-format "heads/$newbranch"}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "We do not like '$newbranch' as a branch name."
		focus $w.desc.name_t
		return
	}

	set rev {}
	switch -- $create_branch_revtype {
	head {set rev $create_branch_head}
	tracking {set rev $create_branch_trackinghead}
	tag {set rev $create_branch_tag}
	expression {set rev $create_branch_revexp}
	}
	if {[catch {set cmt [git rev-parse --verify "${rev}^0"]}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Invalid starting revision: $rev"
		return
	}
	set cmd [list git update-ref]
	lappend cmd -m
	lappend cmd "branch: Created from $rev"
	lappend cmd "refs/heads/$newbranch"
	lappend cmd $cmt
	lappend cmd $null_sha1
	if {[catch {eval exec $cmd} err]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Failed to create '$newbranch'.\n\n$err"
		return
	}

	lappend all_heads $newbranch
	set all_heads [lsort $all_heads]
	populate_branch_menu
	destroy $w
	if {$create_branch_checkout} {
		switch_branch $newbranch
	}
}

proc radio_selector {varname value args} {
	upvar #0 $varname var
	set var $value
}

trace add variable create_branch_head write \
	[list radio_selector create_branch_revtype head]
trace add variable create_branch_trackinghead write \
	[list radio_selector create_branch_revtype tracking]
trace add variable create_branch_tag write \
	[list radio_selector create_branch_revtype tag]

trace add variable delete_branch_head write \
	[list radio_selector delete_branch_checktype head]
trace add variable delete_branch_trackinghead write \
	[list radio_selector delete_branch_checktype tracking]

proc do_create_branch {} {
	global all_heads current_branch repo_config
	global create_branch_checkout create_branch_revtype
	global create_branch_head create_branch_trackinghead
	global create_branch_name create_branch_revexp
	global create_branch_tag

	set w .branch_editor
	toplevel $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	label $w.header -text {Create New Branch} \
		-font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.create -text Create \
		-font font_ui \
		-default active \
		-command [list do_create_branch_action $w]
	pack $w.buttons.create -side right
	button $w.buttons.cancel -text {Cancel} \
		-font font_ui \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	labelframe $w.desc \
		-text {Branch Description} \
		-font font_ui
	label $w.desc.name_l -text {Name:} -font font_ui
	entry $w.desc.name_t \
		-borderwidth 1 \
		-relief sunken \
		-width 40 \
		-textvariable create_branch_name \
		-font font_ui \
		-validate key \
		-validatecommand {
			if {%d == 1 && [regexp {[~^:?*\[\0- ]} %S]} {return 0}
			return 1
		}
	grid $w.desc.name_l $w.desc.name_t -sticky we -padx {0 5}
	grid columnconfigure $w.desc 1 -weight 1
	pack $w.desc -anchor nw -fill x -pady 5 -padx 5

	labelframe $w.from \
		-text {Starting Revision} \
		-font font_ui
	radiobutton $w.from.head_r \
		-text {Local Branch:} \
		-value head \
		-variable create_branch_revtype \
		-font font_ui
	eval tk_optionMenu $w.from.head_m create_branch_head $all_heads
	grid $w.from.head_r $w.from.head_m -sticky w
	set all_trackings [all_tracking_branches]
	if {$all_trackings ne {}} {
		set create_branch_trackinghead [lindex $all_trackings 0]
		radiobutton $w.from.tracking_r \
			-text {Tracking Branch:} \
			-value tracking \
			-variable create_branch_revtype \
			-font font_ui
		eval tk_optionMenu $w.from.tracking_m \
			create_branch_trackinghead \
			$all_trackings
		grid $w.from.tracking_r $w.from.tracking_m -sticky w
	}
	set all_tags [load_all_tags]
	if {$all_tags ne {}} {
		set create_branch_tag [lindex $all_tags 0]
		radiobutton $w.from.tag_r \
			-text {Tag:} \
			-value tag \
			-variable create_branch_revtype \
			-font font_ui
		eval tk_optionMenu $w.from.tag_m \
			create_branch_tag \
			$all_tags
		grid $w.from.tag_r $w.from.tag_m -sticky w
	}
	radiobutton $w.from.exp_r \
		-text {Revision Expression:} \
		-value expression \
		-variable create_branch_revtype \
		-font font_ui
	entry $w.from.exp_t \
		-borderwidth 1 \
		-relief sunken \
		-width 50 \
		-textvariable create_branch_revexp \
		-font font_ui \
		-validate key \
		-validatecommand {
			if {%d == 1 && [regexp {\s} %S]} {return 0}
			if {%d == 1 && [string length %S] > 0} {
				set create_branch_revtype expression
			}
			return 1
		}
	grid $w.from.exp_r $w.from.exp_t -sticky we -padx {0 5}
	grid columnconfigure $w.from 1 -weight 1
	pack $w.from -anchor nw -fill x -pady 5 -padx 5

	labelframe $w.postActions \
		-text {Post Creation Actions} \
		-font font_ui
	checkbutton $w.postActions.checkout \
		-text {Checkout after creation} \
		-variable create_branch_checkout \
		-font font_ui
	pack $w.postActions.checkout -anchor nw
	pack $w.postActions -anchor nw -fill x -pady 5 -padx 5

	set create_branch_checkout 1
	set create_branch_head $current_branch
	set create_branch_revtype head
	set create_branch_name $repo_config(gui.newbranchtemplate)
	set create_branch_revexp {}

	bind $w <Visibility> "
		grab $w
		$w.desc.name_t icursor end
		focus $w.desc.name_t
	"
	bind $w <Key-Escape> "destroy $w"
	bind $w <Key-Return> "do_create_branch_action $w;break"
	wm title $w "[appname] ([reponame]): Create Branch"
	tkwait window $w
}

proc do_delete_branch_action {w} {
	global all_heads
	global delete_branch_checktype delete_branch_head delete_branch_trackinghead

	set check_rev {}
	switch -- $delete_branch_checktype {
	head {set check_rev $delete_branch_head}
	tracking {set check_rev $delete_branch_trackinghead}
	always {set check_rev {:none}}
	}
	if {$check_rev eq {:none}} {
		set check_cmt {}
	} elseif {[catch {set check_cmt [git rev-parse --verify "${check_rev}^0"]}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Invalid check revision: $check_rev"
		return
	}

	set to_delete [list]
	set not_merged [list]
	foreach i [$w.list.l curselection] {
		set b [$w.list.l get $i]
		if {[catch {set o [git rev-parse --verify $b]}]} continue
		if {$check_cmt ne {}} {
			if {$b eq $check_rev} continue
			if {[catch {set m [git merge-base $o $check_cmt]}]} continue
			if {$o ne $m} {
				lappend not_merged $b
				continue
			}
		}
		lappend to_delete [list $b $o]
	}
	if {$not_merged ne {}} {
		set msg "The following branches are not completely merged into $check_rev:

 - [join $not_merged "\n - "]"
		tk_messageBox \
			-icon info \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message $msg
	}
	if {$to_delete eq {}} return
	if {$delete_branch_checktype eq {always}} {
		set msg {Recovering deleted branches is difficult.

Delete the selected branches?}
		if {[tk_messageBox \
			-icon warning \
			-type yesno \
			-title [wm title $w] \
			-parent $w \
			-message $msg] ne yes} {
			return
		}
	}

	set failed {}
	foreach i $to_delete {
		set b [lindex $i 0]
		set o [lindex $i 1]
		if {[catch {git update-ref -d "refs/heads/$b" $o} err]} {
			append failed " - $b: $err\n"
		} else {
			set x [lsearch -sorted -exact $all_heads $b]
			if {$x >= 0} {
				set all_heads [lreplace $all_heads $x $x]
			}
		}
	}

	if {$failed ne {}} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Failed to delete branches:\n$failed"
	}

	set all_heads [lsort $all_heads]
	populate_branch_menu
	destroy $w
}

proc do_delete_branch {} {
	global all_heads tracking_branches current_branch
	global delete_branch_checktype delete_branch_head delete_branch_trackinghead

	set w .branch_editor
	toplevel $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	label $w.header -text {Delete Local Branch} \
		-font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.create -text Delete \
		-font font_ui \
		-command [list do_delete_branch_action $w]
	pack $w.buttons.create -side right
	button $w.buttons.cancel -text {Cancel} \
		-font font_ui \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	labelframe $w.list \
		-text {Local Branches} \
		-font font_ui
	listbox $w.list.l \
		-height 10 \
		-width 70 \
		-selectmode extended \
		-yscrollcommand [list $w.list.sby set] \
		-font font_ui
	foreach h $all_heads {
		if {$h ne $current_branch} {
			$w.list.l insert end $h
		}
	}
	scrollbar $w.list.sby -command [list $w.list.l yview]
	pack $w.list.sby -side right -fill y
	pack $w.list.l -side left -fill both -expand 1
	pack $w.list -fill both -expand 1 -pady 5 -padx 5

	labelframe $w.validate \
		-text {Delete Only If} \
		-font font_ui
	radiobutton $w.validate.head_r \
		-text {Merged Into Local Branch:} \
		-value head \
		-variable delete_branch_checktype \
		-font font_ui
	eval tk_optionMenu $w.validate.head_m delete_branch_head $all_heads
	grid $w.validate.head_r $w.validate.head_m -sticky w
	set all_trackings [all_tracking_branches]
	if {$all_trackings ne {}} {
		set delete_branch_trackinghead [lindex $all_trackings 0]
		radiobutton $w.validate.tracking_r \
			-text {Merged Into Tracking Branch:} \
			-value tracking \
			-variable delete_branch_checktype \
			-font font_ui
		eval tk_optionMenu $w.validate.tracking_m \
			delete_branch_trackinghead \
			$all_trackings
		grid $w.validate.tracking_r $w.validate.tracking_m -sticky w
	}
	radiobutton $w.validate.always_r \
		-text {Always (Do not perform merge checks)} \
		-value always \
		-variable delete_branch_checktype \
		-font font_ui
	grid $w.validate.always_r -columnspan 2 -sticky w
	grid columnconfigure $w.validate 1 -weight 1
	pack $w.validate -anchor nw -fill x -pady 5 -padx 5

	set delete_branch_head $current_branch
	set delete_branch_checktype head

	bind $w <Visibility> "grab $w; focus $w"
	bind $w <Key-Escape> "destroy $w"
	wm title $w "[appname] ([reponame]): Delete Branch"
	tkwait window $w
}

proc switch_branch {new_branch} {
	global HEAD commit_type current_branch repo_config

	if {![lock_index switch]} return

	# -- Our in memory state should match the repository.
	#
	repository_state curType curHEAD curMERGE_HEAD
	if {[string match amend* $commit_type]
		&& $curType eq {normal}
		&& $curHEAD eq $HEAD} {
	} elseif {$commit_type ne $curType || $HEAD ne $curHEAD} {
		info_popup {Last scanned state does not match repository state.

Another Git program has modified this repository
since the last scan.  A rescan must be performed
before the current branch can be changed.

The rescan will be automatically started now.
}
		unlock_index
		rescan {set ui_status_value {Ready.}}
		return
	}

	# -- Don't do a pointless switch.
	#
	if {$current_branch eq $new_branch} {
		unlock_index
		return
	}

	if {$repo_config(gui.trustmtime) eq {true}} {
		switch_branch_stage2 {} $new_branch
	} else {
		set ui_status_value {Refreshing file status...}
		set cmd [list git update-index]
		lappend cmd -q
		lappend cmd --unmerged
		lappend cmd --ignore-missing
		lappend cmd --refresh
		set fd_rf [open "| $cmd" r]
		fconfigure $fd_rf -blocking 0 -translation binary
		fileevent $fd_rf readable \
			[list switch_branch_stage2 $fd_rf $new_branch]
	}
}

proc switch_branch_stage2 {fd_rf new_branch} {
	global ui_status_value HEAD

	if {$fd_rf ne {}} {
		read $fd_rf
		if {![eof $fd_rf]} return
		close $fd_rf
	}

	set ui_status_value "Updating working directory to '$new_branch'..."
	set cmd [list git read-tree]
	lappend cmd -m
	lappend cmd -u
	lappend cmd --exclude-per-directory=.gitignore
	lappend cmd $HEAD
	lappend cmd $new_branch
	set fd_rt [open "| $cmd" r]
	fconfigure $fd_rt -blocking 0 -translation binary
	fileevent $fd_rt readable \
		[list switch_branch_readtree_wait $fd_rt $new_branch]
}

proc switch_branch_readtree_wait {fd_rt new_branch} {
	global selected_commit_type commit_type HEAD MERGE_HEAD PARENT
	global current_branch
	global ui_comm ui_status_value

	# -- We never get interesting output on stdout; only stderr.
	#
	read $fd_rt
	fconfigure $fd_rt -blocking 1
	if {![eof $fd_rt]} {
		fconfigure $fd_rt -blocking 0
		return
	}

	# -- The working directory wasn't in sync with the index and
	#    we'd have to overwrite something to make the switch. A
	#    merge is required.
	#
	if {[catch {close $fd_rt} err]} {
		regsub {^fatal: } $err {} err
		warn_popup "File level merge required.

$err

Staying on branch '$current_branch'."
		set ui_status_value "Aborted checkout of '$new_branch' (file level merging is required)."
		unlock_index
		return
	}

	# -- Update the symbolic ref.  Core git doesn't even check for failure
	#    here, it Just Works(tm).  If it doesn't we are in some really ugly
	#    state that is difficult to recover from within git-gui.
	#
	if {[catch {git symbolic-ref HEAD "refs/heads/$new_branch"} err]} {
		error_popup "Failed to set current branch.

This working directory is only partially switched.
We successfully updated your files, but failed to
update an internal Git file.

This should not have occurred.  [appname] will now
close and give up.

$err"
		do_quit
		return
	}

	# -- Update our repository state.  If we were previously in amend mode
	#    we need to toss the current buffer and do a full rescan to update
	#    our file lists.  If we weren't in amend mode our file lists are
	#    accurate and we can avoid the rescan.
	#
	unlock_index
	set selected_commit_type new
	if {[string match amend* $commit_type]} {
		$ui_comm delete 0.0 end
		$ui_comm edit reset
		$ui_comm edit modified false
		rescan {set ui_status_value "Checked out branch '$current_branch'."}
	} else {
		repository_state commit_type HEAD MERGE_HEAD
		set PARENT $HEAD
		set ui_status_value "Checked out branch '$current_branch'."
	}
}

######################################################################
##
## remote management

proc load_all_remotes {} {
	global repo_config
	global all_remotes tracking_branches

	set all_remotes [list]
	array unset tracking_branches

	set rm_dir [gitdir remotes]
	if {[file isdirectory $rm_dir]} {
		set all_remotes [glob \
			-types f \
			-tails \
			-nocomplain \
			-directory $rm_dir *]

		foreach name $all_remotes {
			catch {
				set fd [open [file join $rm_dir $name] r]
				while {[gets $fd line] >= 0} {
					if {![regexp {^Pull:[ 	]*([^:]+):(.+)$} \
						$line line src dst]} continue
					if {![regexp ^refs/ $dst]} {
						set dst "refs/heads/$dst"
					}
					set tracking_branches($dst) [list $name $src]
				}
				close $fd
			}
		}
	}

	foreach line [array names repo_config remote.*.url] {
		if {![regexp ^remote\.(.*)\.url\$ $line line name]} continue
		lappend all_remotes $name

		if {[catch {set fl $repo_config(remote.$name.fetch)}]} {
			set fl {}
		}
		foreach line $fl {
			if {![regexp {^([^:]+):(.+)$} $line line src dst]} continue
			if {![regexp ^refs/ $dst]} {
				set dst "refs/heads/$dst"
			}
			set tracking_branches($dst) [list $name $src]
		}
	}

	set all_remotes [lsort -unique $all_remotes]
}

proc populate_fetch_menu {} {
	global all_remotes repo_config

	set m .mbar.fetch
	foreach r $all_remotes {
		set enable 0
		if {![catch {set a $repo_config(remote.$r.url)}]} {
			if {![catch {set a $repo_config(remote.$r.fetch)}]} {
				set enable 1
			}
		} else {
			catch {
				set fd [open [gitdir remotes $r] r]
				while {[gets $fd n] >= 0} {
					if {[regexp {^Pull:[ \t]*([^:]+):} $n]} {
						set enable 1
						break
					}
				}
				close $fd
			}
		}

		if {$enable} {
			$m add command \
				-label "Fetch from $r..." \
				-command [list fetch_from $r] \
				-font font_ui
		}
	}
}

proc populate_push_menu {} {
	global all_remotes repo_config

	set m .mbar.push
	set fast_count 0
	foreach r $all_remotes {
		set enable 0
		if {![catch {set a $repo_config(remote.$r.url)}]} {
			if {![catch {set a $repo_config(remote.$r.push)}]} {
				set enable 1
			}
		} else {
			catch {
				set fd [open [gitdir remotes $r] r]
				while {[gets $fd n] >= 0} {
					if {[regexp {^Push:[ \t]*([^:]+):} $n]} {
						set enable 1
						break
					}
				}
				close $fd
			}
		}

		if {$enable} {
			if {!$fast_count} {
				$m add separator
			}
			$m add command \
				-label "Push to $r..." \
				-command [list push_to $r] \
				-font font_ui
			incr fast_count
		}
	}
}

proc start_push_anywhere_action {w} {
	global push_urltype push_remote push_url push_thin push_tags

	set r_url {}
	switch -- $push_urltype {
	remote {set r_url $push_remote}
	url {set r_url $push_url}
	}
	if {$r_url eq {}} return

	set cmd [list git push]
	lappend cmd -v
	if {$push_thin} {
		lappend cmd --thin
	}
	if {$push_tags} {
		lappend cmd --tags
	}
	lappend cmd $r_url
	set cnt 0
	foreach i [$w.source.l curselection] {
		set b [$w.source.l get $i]
		lappend cmd "refs/heads/$b:refs/heads/$b"
		incr cnt
	}
	if {$cnt == 0} {
		return
	} elseif {$cnt == 1} {
		set unit branch
	} else {
		set unit branches
	}

	set cons [new_console "push $r_url" "Pushing $cnt $unit to $r_url"]
	console_exec $cons $cmd console_done
	destroy $w
}

trace add variable push_remote write \
	[list radio_selector push_urltype remote]

proc do_push_anywhere {} {
	global all_heads all_remotes current_branch
	global push_urltype push_remote push_url push_thin push_tags

	set w .push_setup
	toplevel $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	label $w.header -text {Push Branches} -font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.create -text Push \
		-font font_ui \
		-command [list start_push_anywhere_action $w]
	pack $w.buttons.create -side right
	button $w.buttons.cancel -text {Cancel} \
		-font font_ui \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	labelframe $w.source \
		-text {Source Branches} \
		-font font_ui
	listbox $w.source.l \
		-height 10 \
		-width 70 \
		-selectmode extended \
		-yscrollcommand [list $w.source.sby set] \
		-font font_ui
	foreach h $all_heads {
		$w.source.l insert end $h
		if {$h eq $current_branch} {
			$w.source.l select set end
		}
	}
	scrollbar $w.source.sby -command [list $w.source.l yview]
	pack $w.source.sby -side right -fill y
	pack $w.source.l -side left -fill both -expand 1
	pack $w.source -fill both -expand 1 -pady 5 -padx 5

	labelframe $w.dest \
		-text {Destination Repository} \
		-font font_ui
	if {$all_remotes ne {}} {
		radiobutton $w.dest.remote_r \
			-text {Remote:} \
			-value remote \
			-variable push_urltype \
			-font font_ui
		eval tk_optionMenu $w.dest.remote_m push_remote $all_remotes
		grid $w.dest.remote_r $w.dest.remote_m -sticky w
		if {[lsearch -sorted -exact $all_remotes origin] != -1} {
			set push_remote origin
		} else {
			set push_remote [lindex $all_remotes 0]
		}
		set push_urltype remote
	} else {
		set push_urltype url
	}
	radiobutton $w.dest.url_r \
		-text {Arbitrary URL:} \
		-value url \
		-variable push_urltype \
		-font font_ui
	entry $w.dest.url_t \
		-borderwidth 1 \
		-relief sunken \
		-width 50 \
		-textvariable push_url \
		-font font_ui \
		-validate key \
		-validatecommand {
			if {%d == 1 && [regexp {\s} %S]} {return 0}
			if {%d == 1 && [string length %S] > 0} {
				set push_urltype url
			}
			return 1
		}
	grid $w.dest.url_r $w.dest.url_t -sticky we -padx {0 5}
	grid columnconfigure $w.dest 1 -weight 1
	pack $w.dest -anchor nw -fill x -pady 5 -padx 5

	labelframe $w.options \
		-text {Transfer Options} \
		-font font_ui
	checkbutton $w.options.thin \
		-text {Use thin pack (for slow network connections)} \
		-variable push_thin \
		-font font_ui
	grid $w.options.thin -columnspan 2 -sticky w
	checkbutton $w.options.tags \
		-text {Include tags} \
		-variable push_tags \
		-font font_ui
	grid $w.options.tags -columnspan 2 -sticky w
	grid columnconfigure $w.options 1 -weight 1
	pack $w.options -anchor nw -fill x -pady 5 -padx 5

	set push_url {}
	set push_thin 0
	set push_tags 0

	bind $w <Visibility> "grab $w"
	bind $w <Key-Escape> "destroy $w"
	wm title $w "[appname] ([reponame]): Push"
	tkwait window $w
}

######################################################################
##
## merge

proc can_merge {} {
	global HEAD commit_type file_states

	if {[string match amend* $commit_type]} {
		info_popup {Cannot merge while amending.

You must finish amending this commit before
starting any type of merge.
}
		return 0
	}

	if {[committer_ident] eq {}} {return 0}
	if {![lock_index merge]} {return 0}

	# -- Our in memory state should match the repository.
	#
	repository_state curType curHEAD curMERGE_HEAD
	if {$commit_type ne $curType || $HEAD ne $curHEAD} {
		info_popup {Last scanned state does not match repository state.

Another Git program has modified this repository
since the last scan.  A rescan must be performed
before a merge can be performed.

The rescan will be automatically started now.
}
		unlock_index
		rescan {set ui_status_value {Ready.}}
		return 0
	}

	foreach path [array names file_states] {
		switch -glob -- [lindex $file_states($path) 0] {
		_O {
			continue; # and pray it works!
		}
		U? {
			error_popup "You are in the middle of a conflicted merge.

File [short_path $path] has merge conflicts.

You must resolve them, add the file, and commit to
complete the current merge.  Only then can you
begin another merge.
"
			unlock_index
			return 0
		}
		?? {
			error_popup "You are in the middle of a change.

File [short_path $path] is modified.

You should complete the current commit before
starting a merge.  Doing so will help you abort
a failed merge, should the need arise.
"
			unlock_index
			return 0
		}
		}
	}

	return 1
}

proc visualize_local_merge {w} {
	set revs {}
	foreach i [$w.source.l curselection] {
		lappend revs [$w.source.l get $i]
	}
	if {$revs eq {}} return
	lappend revs --not HEAD
	do_gitk $revs
}

proc start_local_merge_action {w} {
	global HEAD ui_status_value current_branch

	set cmd [list git merge]
	set names {}
	set revcnt 0
	foreach i [$w.source.l curselection] {
		set b [$w.source.l get $i]
		lappend cmd $b
		lappend names $b
		incr revcnt
	}

	if {$revcnt == 0} {
		return
	} elseif {$revcnt == 1} {
		set unit branch
	} elseif {$revcnt <= 15} {
		set unit branches
	} else {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Too many branches selected.

You have requested to merge $revcnt branches
in an octopus merge.  This exceeds Git's
internal limit of 15 branches per merge.

Please select fewer branches.  To merge more
than 15 branches, merge the branches in batches.
"
		return
	}

	set msg "Merging $current_branch, [join $names {, }]"
	set ui_status_value "$msg..."
	set cons [new_console "Merge" $msg]
	console_exec $cons $cmd [list finish_merge $revcnt]
	bind $w <Destroy> {}
	destroy $w
}

proc finish_merge {revcnt w ok} {
	console_done $w $ok
	if {$ok} {
		set msg {Merge completed successfully.}
	} else {
		if {$revcnt != 1} {
			info_popup "Octopus merge failed.

Your merge of $revcnt branches has failed.

There are file-level conflicts between the
branches which must be resolved manually.

The working directory will now be reset.

You can attempt this merge again
by merging only one branch at a time." $w

			set fd [open "| git read-tree --reset -u HEAD" r]
			fconfigure $fd -blocking 0 -translation binary
			fileevent $fd readable [list reset_hard_wait $fd]
			set ui_status_value {Aborting... please wait...}
			return
		}

		set msg {Merge failed.  Conflict resolution is required.}
	}
	unlock_index
	rescan [list set ui_status_value $msg]
}

proc do_local_merge {} {
	global current_branch

	if {![can_merge]} return

	set w .merge_setup
	toplevel $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	label $w.header \
		-text "Merge Into $current_branch" \
		-font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.visualize -text Visualize \
		-font font_ui \
		-command [list visualize_local_merge $w]
	pack $w.buttons.visualize -side left
	button $w.buttons.create -text Merge \
		-font font_ui \
		-command [list start_local_merge_action $w]
	pack $w.buttons.create -side right
	button $w.buttons.cancel -text {Cancel} \
		-font font_ui \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	labelframe $w.source \
		-text {Source Branches} \
		-font font_ui
	listbox $w.source.l \
		-height 10 \
		-width 70 \
		-selectmode extended \
		-yscrollcommand [list $w.source.sby set] \
		-font font_ui
	scrollbar $w.source.sby -command [list $w.source.l yview]
	pack $w.source.sby -side right -fill y
	pack $w.source.l -side left -fill both -expand 1
	pack $w.source -fill both -expand 1 -pady 5 -padx 5

	set cmd [list git for-each-ref]
	lappend cmd {--format=%(objectname) %(*objectname) %(refname)}
	lappend cmd refs/heads
	lappend cmd refs/remotes
	lappend cmd refs/tags
	set fr_fd [open "| $cmd" r]
	fconfigure $fr_fd -translation binary
	while {[gets $fr_fd line] > 0} {
		set line [split $line { }]
		set sha1([lindex $line 0]) [lindex $line 2]
		set sha1([lindex $line 1]) [lindex $line 2]
	}
	close $fr_fd

	set to_show {}
	set fr_fd [open "| git rev-list --all --not HEAD"]
	while {[gets $fr_fd line] > 0} {
		if {[catch {set ref $sha1($line)}]} continue
		regsub ^refs/(heads|remotes|tags)/ $ref {} ref
		lappend to_show $ref
	}
	close $fr_fd

	foreach ref [lsort -unique $to_show] {
		$w.source.l insert end $ref
	}

	bind $w <Visibility> "grab $w"
	bind $w <Key-Escape> "unlock_index;destroy $w"
	bind $w <Destroy> unlock_index
	wm title $w "[appname] ([reponame]): Merge"
	tkwait window $w
}

proc do_reset_hard {} {
	global HEAD commit_type file_states

	if {[string match amend* $commit_type]} {
		info_popup {Cannot abort while amending.

You must finish amending this commit.
}
		return
	}

	if {![lock_index abort]} return

	if {[string match *merge* $commit_type]} {
		set op merge
	} else {
		set op commit
	}

	if {[ask_popup "Abort $op?

Aborting the current $op will cause
*ALL* uncommitted changes to be lost.

Continue with aborting the current $op?"] eq {yes}} {
		set fd [open "| git read-tree --reset -u HEAD" r]
		fconfigure $fd -blocking 0 -translation binary
		fileevent $fd readable [list reset_hard_wait $fd]
		set ui_status_value {Aborting... please wait...}
	} else {
		unlock_index
	}
}

proc reset_hard_wait {fd} {
	global ui_comm

	read $fd
	if {[eof $fd]} {
		close $fd
		unlock_index

		$ui_comm delete 0.0 end
		$ui_comm edit modified false

		catch {file delete [gitdir MERGE_HEAD]}
		catch {file delete [gitdir rr-cache MERGE_RR]}
		catch {file delete [gitdir SQUASH_MSG]}
		catch {file delete [gitdir MERGE_MSG]}
		catch {file delete [gitdir GITGUI_MSG]}

		rescan {set ui_status_value {Abort completed.  Ready.}}
	}
}

######################################################################
##
## browser

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
		-yscrollcommand [list $w.list.sby set] \
		-font font_ui
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
		-relief sunken \
		-font font_ui
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
		-relief sunken \
		-font font_ui
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
		-font font_ui \
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

	set blame_status($w) [format \
		"Loading annotations... %i of %i lines annotated (%2i%%)" \
		$blame_data($w,blame_lines) \
		$blame_data($w,total_lines) \
		[expr {100 * $blame_data($w,blame_lines)
			/ $blame_data($w,total_lines)}]]
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
				fconfigure $fd -encoding $enc
				set msg [string trim [read $fd]]
				close $fd
			}
			set blame_data($w,$cmit,message) $msg
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

######################################################################
##
## icons

set filemask {
#define mask_width 14
#define mask_height 15
static unsigned char mask_bits[] = {
   0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f,
   0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f,
   0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f};
}

image create bitmap file_plain -background white -foreground black -data {
#define plain_width 14
#define plain_height 15
static unsigned char plain_bits[] = {
   0xfe, 0x01, 0x02, 0x03, 0x02, 0x05, 0x02, 0x09, 0x02, 0x1f, 0x02, 0x10,
   0x02, 0x10, 0x02, 0x10, 0x02, 0x10, 0x02, 0x10, 0x02, 0x10, 0x02, 0x10,
   0x02, 0x10, 0x02, 0x10, 0xfe, 0x1f};
} -maskdata $filemask

image create bitmap file_mod -background white -foreground blue -data {
#define mod_width 14
#define mod_height 15
static unsigned char mod_bits[] = {
   0xfe, 0x01, 0x02, 0x03, 0x7a, 0x05, 0x02, 0x09, 0x7a, 0x1f, 0x02, 0x10,
   0xfa, 0x17, 0x02, 0x10, 0xfa, 0x17, 0x02, 0x10, 0xfa, 0x17, 0x02, 0x10,
   0xfa, 0x17, 0x02, 0x10, 0xfe, 0x1f};
} -maskdata $filemask

image create bitmap file_fulltick -background white -foreground "#007000" -data {
#define file_fulltick_width 14
#define file_fulltick_height 15
static unsigned char file_fulltick_bits[] = {
   0xfe, 0x01, 0x02, 0x1a, 0x02, 0x0c, 0x02, 0x0c, 0x02, 0x16, 0x02, 0x16,
   0x02, 0x13, 0x00, 0x13, 0x86, 0x11, 0x8c, 0x11, 0xd8, 0x10, 0xf2, 0x10,
   0x62, 0x10, 0x02, 0x10, 0xfe, 0x1f};
} -maskdata $filemask

image create bitmap file_parttick -background white -foreground "#005050" -data {
#define parttick_width 14
#define parttick_height 15
static unsigned char parttick_bits[] = {
   0xfe, 0x01, 0x02, 0x03, 0x7a, 0x05, 0x02, 0x09, 0x7a, 0x1f, 0x02, 0x10,
   0x7a, 0x14, 0x02, 0x16, 0x02, 0x13, 0x8a, 0x11, 0xda, 0x10, 0x72, 0x10,
   0x22, 0x10, 0x02, 0x10, 0xfe, 0x1f};
} -maskdata $filemask

image create bitmap file_question -background white -foreground black -data {
#define file_question_width 14
#define file_question_height 15
static unsigned char file_question_bits[] = {
   0xfe, 0x01, 0x02, 0x02, 0xe2, 0x04, 0xf2, 0x09, 0x1a, 0x1b, 0x0a, 0x13,
   0x82, 0x11, 0xc2, 0x10, 0x62, 0x10, 0x62, 0x10, 0x02, 0x10, 0x62, 0x10,
   0x62, 0x10, 0x02, 0x10, 0xfe, 0x1f};
} -maskdata $filemask

image create bitmap file_removed -background white -foreground red -data {
#define file_removed_width 14
#define file_removed_height 15
static unsigned char file_removed_bits[] = {
   0xfe, 0x01, 0x02, 0x03, 0x02, 0x05, 0x02, 0x09, 0x02, 0x1f, 0x02, 0x10,
   0x1a, 0x16, 0x32, 0x13, 0xe2, 0x11, 0xc2, 0x10, 0xe2, 0x11, 0x32, 0x13,
   0x1a, 0x16, 0x02, 0x10, 0xfe, 0x1f};
} -maskdata $filemask

image create bitmap file_merge -background white -foreground blue -data {
#define file_merge_width 14
#define file_merge_height 15
static unsigned char file_merge_bits[] = {
   0xfe, 0x01, 0x02, 0x03, 0x62, 0x05, 0x62, 0x09, 0x62, 0x1f, 0x62, 0x10,
   0xfa, 0x11, 0xf2, 0x10, 0x62, 0x10, 0x02, 0x10, 0xfa, 0x17, 0x02, 0x10,
   0xfa, 0x17, 0x02, 0x10, 0xfe, 0x1f};
} -maskdata $filemask

set file_dir_data {
#define file_width 18
#define file_height 18
static unsigned char file_bits[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x03, 0x00,
  0x0c, 0x03, 0x00, 0x04, 0xfe, 0x00, 0x06, 0x80, 0x00, 0xff, 0x9f, 0x00,
  0x03, 0x98, 0x00, 0x02, 0x90, 0x00, 0x06, 0xb0, 0x00, 0x04, 0xa0, 0x00,
  0x0c, 0xe0, 0x00, 0x08, 0xc0, 0x00, 0xf8, 0xff, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}
image create bitmap file_dir -background white -foreground blue \
	-data $file_dir_data -maskdata $file_dir_data
unset file_dir_data

set file_uplevel_data {
#define up_width 15
#define up_height 15
static unsigned char up_bits[] = {
  0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x07, 0xf8, 0x0f, 0xfc, 0x1f,
  0xfe, 0x3f, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01,
  0xc0, 0x01, 0xc0, 0x01, 0x00, 0x00};
}
image create bitmap file_uplevel -background white -foreground red \
	-data $file_uplevel_data -maskdata $file_uplevel_data
unset file_uplevel_data

set ui_index .vpane.files.index.list
set ui_workdir .vpane.files.workdir.list

set all_icons(_$ui_index)   file_plain
set all_icons(A$ui_index)   file_fulltick
set all_icons(M$ui_index)   file_fulltick
set all_icons(D$ui_index)   file_removed
set all_icons(U$ui_index)   file_merge

set all_icons(_$ui_workdir) file_plain
set all_icons(M$ui_workdir) file_mod
set all_icons(D$ui_workdir) file_question
set all_icons(U$ui_workdir) file_merge
set all_icons(O$ui_workdir) file_plain

set max_status_desc 0
foreach i {
		{__ "Unmodified"}

		{_M "Modified, not staged"}
		{M_ "Staged for commit"}
		{MM "Portions staged for commit"}
		{MD "Staged for commit, missing"}

		{_O "Untracked, not staged"}
		{A_ "Staged for commit"}
		{AM "Portions staged for commit"}
		{AD "Staged for commit, missing"}

		{_D "Missing"}
		{D_ "Staged for removal"}
		{DO "Staged for removal, still present"}

		{U_ "Requires merge resolution"}
		{UU "Requires merge resolution"}
		{UM "Requires merge resolution"}
		{UD "Requires merge resolution"}
	} {
	if {$max_status_desc < [string length [lindex $i 1]]} {
		set max_status_desc [string length [lindex $i 1]]
	}
	set all_descs([lindex $i 0]) [lindex $i 1]
}
unset i

######################################################################
##
## util

proc bind_button3 {w cmd} {
	bind $w <Any-Button-3> $cmd
	if {[is_MacOSX]} {
		bind $w <Control-Button-1> $cmd
	}
}

proc scrollbar2many {list mode args} {
	foreach w $list {eval $w $mode $args}
}

proc many2scrollbar {list mode sb top bottom} {
	$sb set $top $bottom
	foreach w $list {$w $mode moveto $top}
}

proc incr_font_size {font {amt 1}} {
	set sz [font configure $font -size]
	incr sz $amt
	font configure $font -size $sz
	font configure ${font}bold -size $sz
}

proc hook_failed_popup {hook msg} {
	set w .hookfail
	toplevel $w

	frame $w.m
	label $w.m.l1 -text "$hook hook failed:" \
		-anchor w \
		-justify left \
		-font font_uibold
	text $w.m.t \
		-background white -borderwidth 1 \
		-relief sunken \
		-width 80 -height 10 \
		-font font_diff \
		-yscrollcommand [list $w.m.sby set]
	label $w.m.l2 \
		-text {You must correct the above errors before committing.} \
		-anchor w \
		-justify left \
		-font font_uibold
	scrollbar $w.m.sby -command [list $w.m.t yview]
	pack $w.m.l1 -side top -fill x
	pack $w.m.l2 -side bottom -fill x
	pack $w.m.sby -side right -fill y
	pack $w.m.t -side left -fill both -expand 1
	pack $w.m -side top -fill both -expand 1 -padx 5 -pady 10

	$w.m.t insert 1.0 $msg
	$w.m.t conf -state disabled

	button $w.ok -text OK \
		-width 15 \
		-font font_ui \
		-command "destroy $w"
	pack $w.ok -side bottom -anchor e -pady 10 -padx 10

	bind $w <Visibility> "grab $w; focus $w"
	bind $w <Key-Return> "destroy $w"
	wm title $w "[appname] ([reponame]): error"
	tkwait window $w
}

set next_console_id 0

proc new_console {short_title long_title} {
	global next_console_id console_data
	set w .console[incr next_console_id]
	set console_data($w) [list $short_title $long_title]
	return [console_init $w]
}

proc console_init {w} {
	global console_cr console_data M1B

	set console_cr($w) 1.0
	toplevel $w
	frame $w.m
	label $w.m.l1 -text "[lindex $console_data($w) 1]:" \
		-anchor w \
		-justify left \
		-font font_uibold
	text $w.m.t \
		-background white -borderwidth 1 \
		-relief sunken \
		-width 80 -height 10 \
		-font font_diff \
		-state disabled \
		-yscrollcommand [list $w.m.sby set]
	label $w.m.s -text {Working... please wait...} \
		-anchor w \
		-justify left \
		-font font_uibold
	scrollbar $w.m.sby -command [list $w.m.t yview]
	pack $w.m.l1 -side top -fill x
	pack $w.m.s -side bottom -fill x
	pack $w.m.sby -side right -fill y
	pack $w.m.t -side left -fill both -expand 1
	pack $w.m -side top -fill both -expand 1 -padx 5 -pady 10

	menu $w.ctxm -tearoff 0
	$w.ctxm add command -label "Copy" \
		-font font_ui \
		-command "tk_textCopy $w.m.t"
	$w.ctxm add command -label "Select All" \
		-font font_ui \
		-command "focus $w.m.t;$w.m.t tag add sel 0.0 end"
	$w.ctxm add command -label "Copy All" \
		-font font_ui \
		-command "
			$w.m.t tag add sel 0.0 end
			tk_textCopy $w.m.t
			$w.m.t tag remove sel 0.0 end
		"

	button $w.ok -text {Close} \
		-font font_ui \
		-state disabled \
		-command "destroy $w"
	pack $w.ok -side bottom -anchor e -pady 10 -padx 10

	bind_button3 $w.m.t "tk_popup $w.ctxm %X %Y"
	bind $w.m.t <$M1B-Key-a> "$w.m.t tag add sel 0.0 end;break"
	bind $w.m.t <$M1B-Key-A> "$w.m.t tag add sel 0.0 end;break"
	bind $w <Visibility> "focus $w"
	wm title $w "[appname] ([reponame]): [lindex $console_data($w) 0]"
	return $w
}

proc console_exec {w cmd after} {
	# -- Cygwin's Tcl tosses the enviroment when we exec our child.
	#    But most users need that so we have to relogin. :-(
	#
	if {[is_Cygwin]} {
		set cmd [list sh --login -c "cd \"[pwd]\" && [join $cmd { }]"]
	}

	# -- Tcl won't let us redirect both stdout and stderr to
	#    the same pipe.  So pass it through cat...
	#
	set cmd [concat | $cmd |& cat]

	set fd_f [open $cmd r]
	fconfigure $fd_f -blocking 0 -translation binary
	fileevent $fd_f readable [list console_read $w $fd_f $after]
}

proc console_read {w fd after} {
	global console_cr

	set buf [read $fd]
	if {$buf ne {}} {
		if {![winfo exists $w]} {console_init $w}
		$w.m.t conf -state normal
		set c 0
		set n [string length $buf]
		while {$c < $n} {
			set cr [string first "\r" $buf $c]
			set lf [string first "\n" $buf $c]
			if {$cr < 0} {set cr [expr {$n + 1}]}
			if {$lf < 0} {set lf [expr {$n + 1}]}

			if {$lf < $cr} {
				$w.m.t insert end [string range $buf $c $lf]
				set console_cr($w) [$w.m.t index {end -1c}]
				set c $lf
				incr c
			} else {
				$w.m.t delete $console_cr($w) end
				$w.m.t insert end "\n"
				$w.m.t insert end [string range $buf $c $cr]
				set c $cr
				incr c
			}
		}
		$w.m.t conf -state disabled
		$w.m.t see end
	}

	fconfigure $fd -blocking 1
	if {[eof $fd]} {
		if {[catch {close $fd}]} {
			set ok 0
		} else {
			set ok 1
		}
		uplevel #0 $after $w $ok
		return
	}
	fconfigure $fd -blocking 0
}

proc console_chain {cmdlist w {ok 1}} {
	if {$ok} {
		if {[llength $cmdlist] == 0} {
			console_done $w $ok
			return
		}

		set cmd [lindex $cmdlist 0]
		set cmdlist [lrange $cmdlist 1 end]

		if {[lindex $cmd 0] eq {console_exec}} {
			console_exec $w \
				[lindex $cmd 1] \
				[list console_chain $cmdlist]
		} else {
			uplevel #0 $cmd $cmdlist $w $ok
		}
	} else {
		console_done $w $ok
	}
}

proc console_done {args} {
	global console_cr console_data

	switch -- [llength $args] {
	2 {
		set w [lindex $args 0]
		set ok [lindex $args 1]
	}
	3 {
		set w [lindex $args 1]
		set ok [lindex $args 2]
	}
	default {
		error "wrong number of args: console_done ?ignored? w ok"
	}
	}

	if {$ok} {
		if {[winfo exists $w]} {
			$w.m.s conf -background green -text {Success}
			$w.ok conf -state normal
		}
	} else {
		if {![winfo exists $w]} {
			console_init $w
		}
		$w.m.s conf -background red -text {Error: Command Failed}
		$w.ok conf -state normal
	}

	array unset console_cr $w
	array unset console_data $w
}

######################################################################
##
## ui commands

set starting_gitk_msg {Starting gitk... please wait...}

proc do_gitk {revs} {
	global env ui_status_value starting_gitk_msg

	# -- Always start gitk through whatever we were loaded with.  This
	#    lets us bypass using shell process on Windows systems.
	#
	set cmd [info nameofexecutable]
	lappend cmd [gitexec gitk]
	if {$revs ne {}} {
		append cmd { }
		append cmd $revs
	}

	if {[catch {eval exec $cmd &} err]} {
		error_popup "Failed to start gitk:\n\n$err"
	} else {
		set ui_status_value $starting_gitk_msg
		after 10000 {
			if {$ui_status_value eq $starting_gitk_msg} {
				set ui_status_value {Ready.}
			}
		}
	}
}

proc do_stats {} {
	set fd [open "| git count-objects -v" r]
	while {[gets $fd line] > 0} {
		if {[regexp {^([^:]+): (\d+)$} $line _ name value]} {
			set stats($name) $value
		}
	}
	close $fd

	set packed_sz 0
	foreach p [glob -directory [gitdir objects pack] \
		-type f \
		-nocomplain -- *] {
		incr packed_sz [file size $p]
	}
	if {$packed_sz > 0} {
		set stats(size-pack) [expr {$packed_sz / 1024}]
	}

	set w .stats_view
	toplevel $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	label $w.header -text {Database Statistics} \
		-font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons -border 1
	button $w.buttons.close -text Close \
		-font font_ui \
		-command [list destroy $w]
	button $w.buttons.gc -text {Compress Database} \
		-font font_ui \
		-command "destroy $w;do_gc"
	pack $w.buttons.close -side right
	pack $w.buttons.gc -side left
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	frame $w.stat -borderwidth 1 -relief solid
	foreach s {
		{count           {Number of loose objects}}
		{size            {Disk space used by loose objects} { KiB}}
		{in-pack         {Number of packed objects}}
		{packs           {Number of packs}}
		{size-pack       {Disk space used by packed objects} { KiB}}
		{prune-packable  {Packed objects waiting for pruning}}
		{garbage         {Garbage files}}
		} {
		set name [lindex $s 0]
		set label [lindex $s 1]
		if {[catch {set value $stats($name)}]} continue
		if {[llength $s] > 2} {
			set value "$value[lindex $s 2]"
		}

		label $w.stat.l_$name -text "$label:" -anchor w -font font_ui
		label $w.stat.v_$name -text $value -anchor w -font font_ui
		grid $w.stat.l_$name $w.stat.v_$name -sticky we -padx {0 5}
	}
	pack $w.stat -pady 10 -padx 10

	bind $w <Visibility> "grab $w; focus $w"
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Key-Return> [list destroy $w]
	wm title $w "[appname] ([reponame]): Database Statistics"
	tkwait window $w
}

proc do_gc {} {
	set w [new_console {gc} {Compressing the object database}]
	console_chain {
		{console_exec {git pack-refs --prune}}
		{console_exec {git reflog expire --all}}
		{console_exec {git repack -a -d -l}}
		{console_exec {git rerere gc}}
	} $w
}

proc do_fsck_objects {} {
	set w [new_console {fsck-objects} \
		{Verifying the object database with fsck-objects}]
	set cmd [list git fsck-objects]
	lappend cmd --full
	lappend cmd --cache
	lappend cmd --strict
	console_exec $w $cmd console_done
}

set is_quitting 0

proc do_quit {} {
	global ui_comm is_quitting repo_config commit_type

	if {$is_quitting} return
	set is_quitting 1

	if {[winfo exists $ui_comm]} {
		# -- Stash our current commit buffer.
		#
		set save [gitdir GITGUI_MSG]
		set msg [string trim [$ui_comm get 0.0 end]]
		regsub -all -line {[ \r\t]+$} $msg {} msg
		if {(![string match amend* $commit_type]
			|| [$ui_comm edit modified])
			&& $msg ne {}} {
			catch {
				set fd [open $save w]
				puts -nonewline $fd $msg
				close $fd
			}
		} else {
			catch {file delete $save}
		}

		# -- Stash our current window geometry into this repository.
		#
		set cfg_geometry [list]
		lappend cfg_geometry [wm geometry .]
		lappend cfg_geometry [lindex [.vpane sash coord 0] 1]
		lappend cfg_geometry [lindex [.vpane.files sash coord 0] 0]
		if {[catch {set rc_geometry $repo_config(gui.geometry)}]} {
			set rc_geometry {}
		}
		if {$cfg_geometry ne $rc_geometry} {
			catch {git config gui.geometry $cfg_geometry}
		}
	}

	destroy .
}

proc do_rescan {} {
	rescan {set ui_status_value {Ready.}}
}

proc unstage_helper {txt paths} {
	global file_states current_diff_path

	if {![lock_index begin-update]} return

	set pathList [list]
	set after {}
	foreach path $paths {
		switch -glob -- [lindex $file_states($path) 0] {
		A? -
		M? -
		D? {
			lappend pathList $path
			if {$path eq $current_diff_path} {
				set after {reshow_diff;}
			}
		}
		}
	}
	if {$pathList eq {}} {
		unlock_index
	} else {
		update_indexinfo \
			$txt \
			$pathList \
			[concat $after {set ui_status_value {Ready.}}]
	}
}

proc do_unstage_selection {} {
	global current_diff_path selected_paths

	if {[array size selected_paths] > 0} {
		unstage_helper \
			{Unstaging selected files from commit} \
			[array names selected_paths]
	} elseif {$current_diff_path ne {}} {
		unstage_helper \
			"Unstaging [short_path $current_diff_path] from commit" \
			[list $current_diff_path]
	}
}

proc add_helper {txt paths} {
	global file_states current_diff_path

	if {![lock_index begin-update]} return

	set pathList [list]
	set after {}
	foreach path $paths {
		switch -glob -- [lindex $file_states($path) 0] {
		_O -
		?M -
		?D -
		U? {
			lappend pathList $path
			if {$path eq $current_diff_path} {
				set after {reshow_diff;}
			}
		}
		}
	}
	if {$pathList eq {}} {
		unlock_index
	} else {
		update_index \
			$txt \
			$pathList \
			[concat $after {set ui_status_value {Ready to commit.}}]
	}
}

proc do_add_selection {} {
	global current_diff_path selected_paths

	if {[array size selected_paths] > 0} {
		add_helper \
			{Adding selected files} \
			[array names selected_paths]
	} elseif {$current_diff_path ne {}} {
		add_helper \
			"Adding [short_path $current_diff_path]" \
			[list $current_diff_path]
	}
}

proc do_add_all {} {
	global file_states

	set paths [list]
	foreach path [array names file_states] {
		switch -glob -- [lindex $file_states($path) 0] {
		U? {continue}
		?M -
		?D {lappend paths $path}
		}
	}
	add_helper {Adding all changed files} $paths
}

proc revert_helper {txt paths} {
	global file_states current_diff_path

	if {![lock_index begin-update]} return

	set pathList [list]
	set after {}
	foreach path $paths {
		switch -glob -- [lindex $file_states($path) 0] {
		U? {continue}
		?M -
		?D {
			lappend pathList $path
			if {$path eq $current_diff_path} {
				set after {reshow_diff;}
			}
		}
		}
	}

	set n [llength $pathList]
	if {$n == 0} {
		unlock_index
		return
	} elseif {$n == 1} {
		set s "[short_path [lindex $pathList]]"
	} else {
		set s "these $n files"
	}

	set reply [tk_dialog \
		.confirm_revert \
		"[appname] ([reponame])" \
		"Revert changes in $s?

Any unadded changes will be permanently lost by the revert." \
		question \
		1 \
		{Do Nothing} \
		{Revert Changes} \
		]
	if {$reply == 1} {
		checkout_index \
			$txt \
			$pathList \
			[concat $after {set ui_status_value {Ready.}}]
	} else {
		unlock_index
	}
}

proc do_revert_selection {} {
	global current_diff_path selected_paths

	if {[array size selected_paths] > 0} {
		revert_helper \
			{Reverting selected files} \
			[array names selected_paths]
	} elseif {$current_diff_path ne {}} {
		revert_helper \
			"Reverting [short_path $current_diff_path]" \
			[list $current_diff_path]
	}
}

proc do_signoff {} {
	global ui_comm

	set me [committer_ident]
	if {$me eq {}} return

	set sob "Signed-off-by: $me"
	set last [$ui_comm get {end -1c linestart} {end -1c}]
	if {$last ne $sob} {
		$ui_comm edit separator
		if {$last ne {}
			&& ![regexp {^[A-Z][A-Za-z]*-[A-Za-z-]+: *} $last]} {
			$ui_comm insert end "\n"
		}
		$ui_comm insert end "\n$sob"
		$ui_comm edit separator
		$ui_comm see end
	}
}

proc do_select_commit_type {} {
	global commit_type selected_commit_type

	if {$selected_commit_type eq {new}
		&& [string match amend* $commit_type]} {
		create_new_commit
	} elseif {$selected_commit_type eq {amend}
		&& ![string match amend* $commit_type]} {
		load_last_commit

		# The amend request was rejected...
		#
		if {![string match amend* $commit_type]} {
			set selected_commit_type new
		}
	}
}

proc do_commit {} {
	commit_tree
}

proc do_credits {} {
	global gitgui_credits

	set w .credits_dialog

	toplevel $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	label $w.header -text {git-gui Contributors} -font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.close -text {Close} \
		-font font_ui \
		-command [list destroy $w]
	pack $w.buttons.close -side right
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	frame $w.credits
	text $w.credits.t \
		-background [$w.header cget -background] \
		-yscrollcommand [list $w.credits.sby set] \
		-width 20 \
		-height 10 \
		-wrap none \
		-borderwidth 1 \
		-relief solid \
		-padx 5 -pady 5 \
		-font font_ui
	scrollbar $w.credits.sby -command [list $w.credits.t yview]
	pack $w.credits.sby -side right -fill y
	pack $w.credits.t -fill both -expand 1
	pack $w.credits -side top -fill both -expand 1 -padx 5 -pady 5

	label $w.desc \
		-text "All portions are copyrighted by their respective authors
and are distributed under the GNU General Public License." \
		-padx 5 -pady 5 \
		-justify left \
		-anchor w \
		-borderwidth 1 \
		-relief solid \
		-font font_ui
	pack $w.desc -side top -fill x -padx 5 -pady 5

	$w.credits.t insert end "[string trim $gitgui_credits]\n"
	$w.credits.t conf -state disabled
	$w.credits.t see 1.0

	bind $w <Visibility> "grab $w; focus $w"
	bind $w <Key-Escape> [list destroy $w]
	wm title $w [$w.header cget -text]
	tkwait window $w
}

proc do_about {} {
	global appvers copyright
	global tcl_patchLevel tk_patchLevel

	set w .about_dialog
	toplevel $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	label $w.header -text "About [appname]" \
		-font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.close -text {Close} \
		-font font_ui \
		-command [list destroy $w]
	button $w.buttons.credits -text {Contributors} \
		-font font_ui \
		-command do_credits
	pack $w.buttons.credits -side left
	pack $w.buttons.close -side right
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	label $w.desc \
		-text "git-gui - a graphical user interface for Git.
$copyright" \
		-padx 5 -pady 5 \
		-justify left \
		-anchor w \
		-borderwidth 1 \
		-relief solid \
		-font font_ui
	pack $w.desc -side top -fill x -padx 5 -pady 5

	set v {}
	append v "git-gui version $appvers\n"
	append v "[git version]\n"
	append v "\n"
	if {$tcl_patchLevel eq $tk_patchLevel} {
		append v "Tcl/Tk version $tcl_patchLevel"
	} else {
		append v "Tcl version $tcl_patchLevel"
		append v ", Tk version $tk_patchLevel"
	}

	label $w.vers \
		-text $v \
		-padx 5 -pady 5 \
		-justify left \
		-anchor w \
		-borderwidth 1 \
		-relief solid \
		-font font_ui
	pack $w.vers -side top -fill x -padx 5 -pady 5

	menu $w.ctxm -tearoff 0
	$w.ctxm add command \
		-label {Copy} \
		-font font_ui \
		-command "
		clipboard clear
		clipboard append -format STRING -type STRING -- \[$w.vers cget -text\]
	"

	bind $w <Visibility> "grab $w; focus $w"
	bind $w <Key-Escape> "destroy $w"
	bind_button3 $w.vers "tk_popup $w.ctxm %X %Y; grab $w; focus $w"
	wm title $w "About [appname]"
	tkwait window $w
}

proc do_options {} {
	global repo_config global_config font_descs
	global repo_config_new global_config_new

	array unset repo_config_new
	array unset global_config_new
	foreach name [array names repo_config] {
		set repo_config_new($name) $repo_config($name)
	}
	load_config 1
	foreach name [array names repo_config] {
		switch -- $name {
		gui.diffcontext {continue}
		}
		set repo_config_new($name) $repo_config($name)
	}
	foreach name [array names global_config] {
		set global_config_new($name) $global_config($name)
	}

	set w .options_editor
	toplevel $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	label $w.header -text "Options" \
		-font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.restore -text {Restore Defaults} \
		-font font_ui \
		-command do_restore_defaults
	pack $w.buttons.restore -side left
	button $w.buttons.save -text Save \
		-font font_ui \
		-command [list do_save_config $w]
	pack $w.buttons.save -side right
	button $w.buttons.cancel -text {Cancel} \
		-font font_ui \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	labelframe $w.repo -text "[reponame] Repository" \
		-font font_ui
	labelframe $w.global -text {Global (All Repositories)} \
		-font font_ui
	pack $w.repo -side left -fill both -expand 1 -pady 5 -padx 5
	pack $w.global -side right -fill both -expand 1 -pady 5 -padx 5

	set optid 0
	foreach option {
		{t user.name {User Name}}
		{t user.email {Email Address}}

		{b merge.summary {Summarize Merge Commits}}
		{i-1..5 merge.verbosity {Merge Verbosity}}

		{b gui.trustmtime  {Trust File Modification Timestamps}}
		{i-1..99 gui.diffcontext {Number of Diff Context Lines}}
		{t gui.newbranchtemplate {New Branch Name Template}}
		} {
		set type [lindex $option 0]
		set name [lindex $option 1]
		set text [lindex $option 2]
		incr optid
		foreach f {repo global} {
			switch -glob -- $type {
			b {
				checkbutton $w.$f.$optid -text $text \
					-variable ${f}_config_new($name) \
					-onvalue true \
					-offvalue false \
					-font font_ui
				pack $w.$f.$optid -side top -anchor w
			}
			i-* {
				regexp -- {-(\d+)\.\.(\d+)$} $type _junk min max
				frame $w.$f.$optid
				label $w.$f.$optid.l -text "$text:" -font font_ui
				pack $w.$f.$optid.l -side left -anchor w -fill x
				spinbox $w.$f.$optid.v \
					-textvariable ${f}_config_new($name) \
					-from $min \
					-to $max \
					-increment 1 \
					-width [expr {1 + [string length $max]}] \
					-font font_ui
				bind $w.$f.$optid.v <FocusIn> {%W selection range 0 end}
				pack $w.$f.$optid.v -side right -anchor e -padx 5
				pack $w.$f.$optid -side top -anchor w -fill x
			}
			t {
				frame $w.$f.$optid
				label $w.$f.$optid.l -text "$text:" -font font_ui
				entry $w.$f.$optid.v \
					-borderwidth 1 \
					-relief sunken \
					-width 20 \
					-textvariable ${f}_config_new($name) \
					-font font_ui
				pack $w.$f.$optid.l -side left -anchor w
				pack $w.$f.$optid.v -side left -anchor w \
					-fill x -expand 1 \
					-padx 5
				pack $w.$f.$optid -side top -anchor w -fill x
			}
			}
		}
	}

	set all_fonts [lsort [font families]]
	foreach option $font_descs {
		set name [lindex $option 0]
		set font [lindex $option 1]
		set text [lindex $option 2]

		set global_config_new(gui.$font^^family) \
			[font configure $font -family]
		set global_config_new(gui.$font^^size) \
			[font configure $font -size]

		frame $w.global.$name
		label $w.global.$name.l -text "$text:" -font font_ui
		pack $w.global.$name.l -side left -anchor w -fill x
		eval tk_optionMenu $w.global.$name.family \
			global_config_new(gui.$font^^family) \
			$all_fonts
		spinbox $w.global.$name.size \
			-textvariable global_config_new(gui.$font^^size) \
			-from 2 -to 80 -increment 1 \
			-width 3 \
			-font font_ui
		bind $w.global.$name.size <FocusIn> {%W selection range 0 end}
		pack $w.global.$name.size -side right -anchor e
		pack $w.global.$name.family -side right -anchor e
		pack $w.global.$name -side top -anchor w -fill x
	}

	bind $w <Visibility> "grab $w; focus $w"
	bind $w <Key-Escape> "destroy $w"
	wm title $w "[appname] ([reponame]): Options"
	tkwait window $w
}

proc do_restore_defaults {} {
	global font_descs default_config repo_config
	global repo_config_new global_config_new

	foreach name [array names default_config] {
		set repo_config_new($name) $default_config($name)
		set global_config_new($name) $default_config($name)
	}

	foreach option $font_descs {
		set name [lindex $option 0]
		set repo_config(gui.$name) $default_config(gui.$name)
	}
	apply_config

	foreach option $font_descs {
		set name [lindex $option 0]
		set font [lindex $option 1]
		set global_config_new(gui.$font^^family) \
			[font configure $font -family]
		set global_config_new(gui.$font^^size) \
			[font configure $font -size]
	}
}

proc do_save_config {w} {
	if {[catch {save_config} err]} {
		error_popup "Failed to completely save options:\n\n$err"
	}
	reshow_diff
	destroy $w
}

proc do_windows_shortcut {} {
	global argv0

	set fn [tk_getSaveFile \
		-parent . \
		-title "[appname] ([reponame]): Create Desktop Icon" \
		-initialfile "Git [reponame].bat"]
	if {$fn != {}} {
		if {[catch {
				set fd [open $fn w]
				puts $fd "@ECHO Entering [reponame]"
				puts $fd "@ECHO Starting git-gui... please wait..."
				puts $fd "@SET PATH=[file normalize [gitexec]];%PATH%"
				puts $fd "@SET GIT_DIR=[file normalize [gitdir]]"
				puts -nonewline $fd "@\"[info nameofexecutable]\""
				puts $fd " \"[file normalize $argv0]\""
				close $fd
			} err]} {
			error_popup "Cannot write script:\n\n$err"
		}
	}
}

proc do_cygwin_shortcut {} {
	global argv0

	if {[catch {
		set desktop [exec cygpath \
			--windows \
			--absolute \
			--long-name \
			--desktop]
		}]} {
			set desktop .
	}
	set fn [tk_getSaveFile \
		-parent . \
		-title "[appname] ([reponame]): Create Desktop Icon" \
		-initialdir $desktop \
		-initialfile "Git [reponame].bat"]
	if {$fn != {}} {
		if {[catch {
				set fd [open $fn w]
				set sh [exec cygpath \
					--windows \
					--absolute \
					/bin/sh]
				set me [exec cygpath \
					--unix \
					--absolute \
					$argv0]
				set gd [exec cygpath \
					--unix \
					--absolute \
					[gitdir]]
				set gw [exec cygpath \
					--windows \
					--absolute \
					[file dirname [gitdir]]]
				regsub -all ' $me "'\\''" me
				regsub -all ' $gd "'\\''" gd
				puts $fd "@ECHO Entering $gw"
				puts $fd "@ECHO Starting git-gui... please wait..."
				puts -nonewline $fd "@\"$sh\" --login -c \""
				puts -nonewline $fd "GIT_DIR='$gd'"
				puts -nonewline $fd " '$me'"
				puts $fd "&\""
				close $fd
			} err]} {
			error_popup "Cannot write script:\n\n$err"
		}
	}
}

proc do_macosx_app {} {
	global argv0 env

	set fn [tk_getSaveFile \
		-parent . \
		-title "[appname] ([reponame]): Create Desktop Icon" \
		-initialdir [file join $env(HOME) Desktop] \
		-initialfile "Git [reponame].app"]
	if {$fn != {}} {
		if {[catch {
				set Contents [file join $fn Contents]
				set MacOS [file join $Contents MacOS]
				set exe [file join $MacOS git-gui]

				file mkdir $MacOS

				set fd [open [file join $Contents Info.plist] w]
				puts $fd {<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>English</string>
	<key>CFBundleExecutable</key>
	<string>git-gui</string>
	<key>CFBundleIdentifier</key>
	<string>org.spearce.git-gui</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleVersion</key>
	<string>1.0</string>
	<key>NSPrincipalClass</key>
	<string>NSApplication</string>
</dict>
</plist>}
				close $fd

				set fd [open $exe w]
				set gd [file normalize [gitdir]]
				set ep [file normalize [gitexec]]
				regsub -all ' $gd "'\\''" gd
				regsub -all ' $ep "'\\''" ep
				puts $fd "#!/bin/sh"
				foreach name [array names env] {
					if {[string match GIT_* $name]} {
						regsub -all ' $env($name) "'\\''" v
						puts $fd "export $name='$v'"
					}
				}
				puts $fd "export PATH='$ep':\$PATH"
				puts $fd "export GIT_DIR='$gd'"
				puts $fd "exec [file normalize $argv0]"
				close $fd

				file attributes $exe -permissions u+x,g+x,o+x
			} err]} {
			error_popup "Cannot write icon:\n\n$err"
		}
	}
}

proc toggle_or_diff {w x y} {
	global file_states file_lists current_diff_path ui_index ui_workdir
	global last_clicked selected_paths

	set pos [split [$w index @$x,$y] .]
	set lno [lindex $pos 0]
	set col [lindex $pos 1]
	set path [lindex $file_lists($w) [expr {$lno - 1}]]
	if {$path eq {}} {
		set last_clicked {}
		return
	}

	set last_clicked [list $w $lno]
	array unset selected_paths
	$ui_index tag remove in_sel 0.0 end
	$ui_workdir tag remove in_sel 0.0 end

	if {$col == 0} {
		if {$current_diff_path eq $path} {
			set after {reshow_diff;}
		} else {
			set after {}
		}
		if {$w eq $ui_index} {
			update_indexinfo \
				"Unstaging [short_path $path] from commit" \
				[list $path] \
				[concat $after {set ui_status_value {Ready.}}]
		} elseif {$w eq $ui_workdir} {
			update_index \
				"Adding [short_path $path]" \
				[list $path] \
				[concat $after {set ui_status_value {Ready.}}]
		}
	} else {
		show_diff $path $w $lno
	}
}

proc add_one_to_selection {w x y} {
	global file_lists last_clicked selected_paths

	set lno [lindex [split [$w index @$x,$y] .] 0]
	set path [lindex $file_lists($w) [expr {$lno - 1}]]
	if {$path eq {}} {
		set last_clicked {}
		return
	}

	if {$last_clicked ne {}
		&& [lindex $last_clicked 0] ne $w} {
		array unset selected_paths
		[lindex $last_clicked 0] tag remove in_sel 0.0 end
	}

	set last_clicked [list $w $lno]
	if {[catch {set in_sel $selected_paths($path)}]} {
		set in_sel 0
	}
	if {$in_sel} {
		unset selected_paths($path)
		$w tag remove in_sel $lno.0 [expr {$lno + 1}].0
	} else {
		set selected_paths($path) 1
		$w tag add in_sel $lno.0 [expr {$lno + 1}].0
	}
}

proc add_range_to_selection {w x y} {
	global file_lists last_clicked selected_paths

	if {[lindex $last_clicked 0] ne $w} {
		toggle_or_diff $w $x $y
		return
	}

	set lno [lindex [split [$w index @$x,$y] .] 0]
	set lc [lindex $last_clicked 1]
	if {$lc < $lno} {
		set begin $lc
		set end $lno
	} else {
		set begin $lno
		set end $lc
	}

	foreach path [lrange $file_lists($w) \
		[expr {$begin - 1}] \
		[expr {$end - 1}]] {
		set selected_paths($path) 1
	}
	$w tag add in_sel $begin.0 [expr {$end + 1}].0
}

######################################################################
##
## config defaults

set cursor_ptr arrow
font create font_diff -family Courier -size 10
font create font_ui
catch {
	label .dummy
	eval font configure font_ui [font actual [.dummy cget -font]]
	destroy .dummy
}

font create font_uibold
font create font_diffbold

if {[is_Windows]} {
	set M1B Control
	set M1T Ctrl
} elseif {[is_MacOSX]} {
	set M1B M1
	set M1T Cmd
} else {
	set M1B M1
	set M1T M1
}

proc apply_config {} {
	global repo_config font_descs

	foreach option $font_descs {
		set name [lindex $option 0]
		set font [lindex $option 1]
		if {[catch {
			foreach {cn cv} $repo_config(gui.$name) {
				font configure $font $cn $cv
			}
			} err]} {
			error_popup "Invalid font specified in gui.$name:\n\n$err"
		}
		foreach {cn cv} [font configure $font] {
			font configure ${font}bold $cn $cv
		}
		font configure ${font}bold -weight bold
	}
}

set default_config(merge.summary) false
set default_config(merge.verbosity) 2
set default_config(user.name) {}
set default_config(user.email) {}

set default_config(gui.trustmtime) false
set default_config(gui.diffcontext) 5
set default_config(gui.newbranchtemplate) {}
set default_config(gui.fontui) [font configure font_ui]
set default_config(gui.fontdiff) [font configure font_diff]
set font_descs {
	{fontui   font_ui   {Main Font}}
	{fontdiff font_diff {Diff/Console Font}}
}
load_config 0
apply_config

######################################################################
##
## feature option selection

if {[regexp {^git-(.+)$} [appname] _junk subcommand]} {
	unset _junk
} else {
	set subcommand gui
}
if {$subcommand eq {gui.sh}} {
	set subcommand gui
}
if {$subcommand eq {gui} && [llength $argv] > 0} {
	set subcommand [lindex $argv 0]
	set argv [lrange $argv 1 end]
}

enable_option multicommit
enable_option branch
enable_option transport

switch -- $subcommand {
--version -
version -
browser -
blame {
	disable_option multicommit
	disable_option branch
	disable_option transport
}
citool {
	enable_option singlecommit

	disable_option multicommit
	disable_option branch
	disable_option transport
}
}

######################################################################
##
## ui construction

set ui_comm {}

# -- Menu Bar
#
menu .mbar -tearoff 0
.mbar add cascade -label Repository -menu .mbar.repository
.mbar add cascade -label Edit -menu .mbar.edit
if {[is_enabled branch]} {
	.mbar add cascade -label Branch -menu .mbar.branch
}
if {[is_enabled multicommit] || [is_enabled singlecommit]} {
	.mbar add cascade -label Commit -menu .mbar.commit
}
if {[is_enabled transport]} {
	.mbar add cascade -label Merge -menu .mbar.merge
	.mbar add cascade -label Fetch -menu .mbar.fetch
	.mbar add cascade -label Push -menu .mbar.push
}
. configure -menu .mbar

# -- Repository Menu
#
menu .mbar.repository

.mbar.repository add command \
	-label {Browse Current Branch} \
	-command {new_browser $current_branch} \
	-font font_ui
trace add variable current_branch write ".mbar.repository entryconf [.mbar.repository index last] -label \"Browse \$current_branch\" ;#"
.mbar.repository add separator

.mbar.repository add command \
	-label {Visualize Current Branch} \
	-command {do_gitk $current_branch} \
	-font font_ui
trace add variable current_branch write ".mbar.repository entryconf [.mbar.repository index last] -label \"Visualize \$current_branch\" ;#"
.mbar.repository add command \
	-label {Visualize All Branches} \
	-command {do_gitk --all} \
	-font font_ui
.mbar.repository add separator

if {[is_enabled multicommit]} {
	.mbar.repository add command -label {Database Statistics} \
		-command do_stats \
		-font font_ui

	.mbar.repository add command -label {Compress Database} \
		-command do_gc \
		-font font_ui

	.mbar.repository add command -label {Verify Database} \
		-command do_fsck_objects \
		-font font_ui

	.mbar.repository add separator

	if {[is_Cygwin]} {
		.mbar.repository add command \
			-label {Create Desktop Icon} \
			-command do_cygwin_shortcut \
			-font font_ui
	} elseif {[is_Windows]} {
		.mbar.repository add command \
			-label {Create Desktop Icon} \
			-command do_windows_shortcut \
			-font font_ui
	} elseif {[is_MacOSX]} {
		.mbar.repository add command \
			-label {Create Desktop Icon} \
			-command do_macosx_app \
			-font font_ui
	}
}

.mbar.repository add command -label Quit \
	-command do_quit \
	-accelerator $M1T-Q \
	-font font_ui

# -- Edit Menu
#
menu .mbar.edit
.mbar.edit add command -label Undo \
	-command {catch {[focus] edit undo}} \
	-accelerator $M1T-Z \
	-font font_ui
.mbar.edit add command -label Redo \
	-command {catch {[focus] edit redo}} \
	-accelerator $M1T-Y \
	-font font_ui
.mbar.edit add separator
.mbar.edit add command -label Cut \
	-command {catch {tk_textCut [focus]}} \
	-accelerator $M1T-X \
	-font font_ui
.mbar.edit add command -label Copy \
	-command {catch {tk_textCopy [focus]}} \
	-accelerator $M1T-C \
	-font font_ui
.mbar.edit add command -label Paste \
	-command {catch {tk_textPaste [focus]; [focus] see insert}} \
	-accelerator $M1T-V \
	-font font_ui
.mbar.edit add command -label Delete \
	-command {catch {[focus] delete sel.first sel.last}} \
	-accelerator Del \
	-font font_ui
.mbar.edit add separator
.mbar.edit add command -label {Select All} \
	-command {catch {[focus] tag add sel 0.0 end}} \
	-accelerator $M1T-A \
	-font font_ui

# -- Branch Menu
#
if {[is_enabled branch]} {
	menu .mbar.branch

	.mbar.branch add command -label {Create...} \
		-command do_create_branch \
		-accelerator $M1T-N \
		-font font_ui
	lappend disable_on_lock [list .mbar.branch entryconf \
		[.mbar.branch index last] -state]

	.mbar.branch add command -label {Delete...} \
		-command do_delete_branch \
		-font font_ui
	lappend disable_on_lock [list .mbar.branch entryconf \
		[.mbar.branch index last] -state]
}

# -- Commit Menu
#
if {[is_enabled multicommit] || [is_enabled singlecommit]} {
	menu .mbar.commit

	.mbar.commit add radiobutton \
		-label {New Commit} \
		-command do_select_commit_type \
		-variable selected_commit_type \
		-value new \
		-font font_ui
	lappend disable_on_lock \
		[list .mbar.commit entryconf [.mbar.commit index last] -state]

	.mbar.commit add radiobutton \
		-label {Amend Last Commit} \
		-command do_select_commit_type \
		-variable selected_commit_type \
		-value amend \
		-font font_ui
	lappend disable_on_lock \
		[list .mbar.commit entryconf [.mbar.commit index last] -state]

	.mbar.commit add separator

	.mbar.commit add command -label Rescan \
		-command do_rescan \
		-accelerator F5 \
		-font font_ui
	lappend disable_on_lock \
		[list .mbar.commit entryconf [.mbar.commit index last] -state]

	.mbar.commit add command -label {Add To Commit} \
		-command do_add_selection \
		-font font_ui
	lappend disable_on_lock \
		[list .mbar.commit entryconf [.mbar.commit index last] -state]

	.mbar.commit add command -label {Add Existing To Commit} \
		-command do_add_all \
		-accelerator $M1T-I \
		-font font_ui
	lappend disable_on_lock \
		[list .mbar.commit entryconf [.mbar.commit index last] -state]

	.mbar.commit add command -label {Unstage From Commit} \
		-command do_unstage_selection \
		-font font_ui
	lappend disable_on_lock \
		[list .mbar.commit entryconf [.mbar.commit index last] -state]

	.mbar.commit add command -label {Revert Changes} \
		-command do_revert_selection \
		-font font_ui
	lappend disable_on_lock \
		[list .mbar.commit entryconf [.mbar.commit index last] -state]

	.mbar.commit add separator

	.mbar.commit add command -label {Sign Off} \
		-command do_signoff \
		-accelerator $M1T-S \
		-font font_ui

	.mbar.commit add command -label Commit \
		-command do_commit \
		-accelerator $M1T-Return \
		-font font_ui
	lappend disable_on_lock \
		[list .mbar.commit entryconf [.mbar.commit index last] -state]
}

if {[is_MacOSX]} {
	# -- Apple Menu (Mac OS X only)
	#
	.mbar add cascade -label Apple -menu .mbar.apple
	menu .mbar.apple

	.mbar.apple add command -label "About [appname]" \
		-command do_about \
		-font font_ui
	.mbar.apple add command -label "Options..." \
		-command do_options \
		-font font_ui
} else {
	# -- Edit Menu
	#
	.mbar.edit add separator
	.mbar.edit add command -label {Options...} \
		-command do_options \
		-font font_ui

	# -- Tools Menu
	#
	if {[file exists /usr/local/miga/lib/gui-miga]
		&& [file exists .pvcsrc]} {
	proc do_miga {} {
		global ui_status_value
		if {![lock_index update]} return
		set cmd [list sh --login -c "/usr/local/miga/lib/gui-miga \"[pwd]\""]
		set miga_fd [open "|$cmd" r]
		fconfigure $miga_fd -blocking 0
		fileevent $miga_fd readable [list miga_done $miga_fd]
		set ui_status_value {Running miga...}
	}
	proc miga_done {fd} {
		read $fd 512
		if {[eof $fd]} {
			close $fd
			unlock_index
			rescan [list set ui_status_value {Ready.}]
		}
	}
	.mbar add cascade -label Tools -menu .mbar.tools
	menu .mbar.tools
	.mbar.tools add command -label "Migrate" \
		-command do_miga \
		-font font_ui
	lappend disable_on_lock \
		[list .mbar.tools entryconf [.mbar.tools index last] -state]
	}
}

# -- Help Menu
#
.mbar add cascade -label Help -menu .mbar.help
menu .mbar.help

if {![is_MacOSX]} {
	.mbar.help add command -label "About [appname]" \
		-command do_about \
		-font font_ui
}

set browser {}
catch {set browser $repo_config(instaweb.browser)}
set doc_path [file dirname [gitexec]]
set doc_path [file join $doc_path Documentation index.html]

if {[is_Cygwin]} {
	set doc_path [exec cygpath --mixed $doc_path]
}

if {$browser eq {}} {
	if {[is_MacOSX]} {
		set browser open
	} elseif {[is_Cygwin]} {
		set program_files [file dirname [exec cygpath --windir]]
		set program_files [file join $program_files {Program Files}]
		set firefox [file join $program_files {Mozilla Firefox} firefox.exe]
		set ie [file join $program_files {Internet Explorer} IEXPLORE.EXE]
		if {[file exists $firefox]} {
			set browser $firefox
		} elseif {[file exists $ie]} {
			set browser $ie
		}
		unset program_files firefox ie
	}
}

if {[file isfile $doc_path]} {
	set doc_url "file:$doc_path"
} else {
	set doc_url {http://www.kernel.org/pub/software/scm/git/docs/}
}

if {$browser ne {}} {
	.mbar.help add command -label {Online Documentation} \
		-command [list exec $browser $doc_url &] \
		-font font_ui
}
unset browser doc_path doc_url

# -- Standard bindings
#
bind .   <Destroy> do_quit
bind all <$M1B-Key-q> do_quit
bind all <$M1B-Key-Q> do_quit
bind all <$M1B-Key-w> {destroy [winfo toplevel %W]}
bind all <$M1B-Key-W> {destroy [winfo toplevel %W]}

# -- Not a normal commit type invocation?  Do that instead!
#
switch -- $subcommand {
--version -
version {
	puts "git-gui version $appvers"
	exit
}
browser {
	if {[llength $argv] != 1} {
		puts stderr "usage: $argv0 browser commit"
		exit 1
	}
	set current_branch [lindex $argv 0]
	new_browser $current_branch
	return
}
blame {
	if {[llength $argv] != 2} {
		puts stderr "usage: $argv0 blame commit path"
		exit 1
	}
	set current_branch [lindex $argv 0]
	show_blame $current_branch [lindex $argv 1]
	return
}
citool -
gui {
	if {[llength $argv] != 0} {
		puts -nonewline stderr "usage: $argv0"
		if {$subcommand ne {gui} && [appname] ne "git-$subcommand"} {
			puts -nonewline stderr " $subcommand"
		}
		puts stderr {}
		exit 1
	}
	# fall through to setup UI for commits
}
default {
	puts stderr "usage: $argv0 \[{blame|browser|citool}\]"
	exit 1
}
}

# -- Branch Control
#
frame .branch \
	-borderwidth 1 \
	-relief sunken
label .branch.l1 \
	-text {Current Branch:} \
	-anchor w \
	-justify left \
	-font font_ui
label .branch.cb \
	-textvariable current_branch \
	-anchor w \
	-justify left \
	-font font_ui
pack .branch.l1 -side left
pack .branch.cb -side left -fill x
pack .branch -side top -fill x

if {[is_enabled branch]} {
	menu .mbar.merge
	.mbar.merge add command -label {Local Merge...} \
		-command do_local_merge \
		-font font_ui
	lappend disable_on_lock \
		[list .mbar.merge entryconf [.mbar.merge index last] -state]
	.mbar.merge add command -label {Abort Merge...} \
		-command do_reset_hard \
		-font font_ui
	lappend disable_on_lock \
		[list .mbar.merge entryconf [.mbar.merge index last] -state]


	menu .mbar.fetch

	menu .mbar.push
	.mbar.push add command -label {Push...} \
		-command do_push_anywhere \
		-font font_ui
}

# -- Main Window Layout
#
panedwindow .vpane -orient vertical
panedwindow .vpane.files -orient horizontal
.vpane add .vpane.files -sticky nsew -height 100 -width 200
pack .vpane -anchor n -side top -fill both -expand 1

# -- Index File List
#
frame .vpane.files.index -height 100 -width 200
label .vpane.files.index.title -text {Changes To Be Committed} \
	-background green \
	-font font_ui
text $ui_index -background white -borderwidth 0 \
	-width 20 -height 10 \
	-wrap none \
	-font font_ui \
	-cursor $cursor_ptr \
	-xscrollcommand {.vpane.files.index.sx set} \
	-yscrollcommand {.vpane.files.index.sy set} \
	-state disabled
scrollbar .vpane.files.index.sx -orient h -command [list $ui_index xview]
scrollbar .vpane.files.index.sy -orient v -command [list $ui_index yview]
pack .vpane.files.index.title -side top -fill x
pack .vpane.files.index.sx -side bottom -fill x
pack .vpane.files.index.sy -side right -fill y
pack $ui_index -side left -fill both -expand 1
.vpane.files add .vpane.files.index -sticky nsew

# -- Working Directory File List
#
frame .vpane.files.workdir -height 100 -width 200
label .vpane.files.workdir.title -text {Changed But Not Updated} \
	-background red \
	-font font_ui
text $ui_workdir -background white -borderwidth 0 \
	-width 20 -height 10 \
	-wrap none \
	-font font_ui \
	-cursor $cursor_ptr \
	-xscrollcommand {.vpane.files.workdir.sx set} \
	-yscrollcommand {.vpane.files.workdir.sy set} \
	-state disabled
scrollbar .vpane.files.workdir.sx -orient h -command [list $ui_workdir xview]
scrollbar .vpane.files.workdir.sy -orient v -command [list $ui_workdir yview]
pack .vpane.files.workdir.title -side top -fill x
pack .vpane.files.workdir.sx -side bottom -fill x
pack .vpane.files.workdir.sy -side right -fill y
pack $ui_workdir -side left -fill both -expand 1
.vpane.files add .vpane.files.workdir -sticky nsew

foreach i [list $ui_index $ui_workdir] {
	$i tag conf in_diff -font font_uibold
	$i tag conf in_sel \
		-background [$i cget -foreground] \
		-foreground [$i cget -background]
}
unset i

# -- Diff and Commit Area
#
frame .vpane.lower -height 300 -width 400
frame .vpane.lower.commarea
frame .vpane.lower.diff -relief sunken -borderwidth 1
pack .vpane.lower.commarea -side top -fill x
pack .vpane.lower.diff -side bottom -fill both -expand 1
.vpane add .vpane.lower -sticky nsew

# -- Commit Area Buttons
#
frame .vpane.lower.commarea.buttons
label .vpane.lower.commarea.buttons.l -text {} \
	-anchor w \
	-justify left \
	-font font_ui
pack .vpane.lower.commarea.buttons.l -side top -fill x
pack .vpane.lower.commarea.buttons -side left -fill y

button .vpane.lower.commarea.buttons.rescan -text {Rescan} \
	-command do_rescan \
	-font font_ui
pack .vpane.lower.commarea.buttons.rescan -side top -fill x
lappend disable_on_lock \
	{.vpane.lower.commarea.buttons.rescan conf -state}

button .vpane.lower.commarea.buttons.incall -text {Add Existing} \
	-command do_add_all \
	-font font_ui
pack .vpane.lower.commarea.buttons.incall -side top -fill x
lappend disable_on_lock \
	{.vpane.lower.commarea.buttons.incall conf -state}

button .vpane.lower.commarea.buttons.signoff -text {Sign Off} \
	-command do_signoff \
	-font font_ui
pack .vpane.lower.commarea.buttons.signoff -side top -fill x

button .vpane.lower.commarea.buttons.commit -text {Commit} \
	-command do_commit \
	-font font_ui
pack .vpane.lower.commarea.buttons.commit -side top -fill x
lappend disable_on_lock \
	{.vpane.lower.commarea.buttons.commit conf -state}

# -- Commit Message Buffer
#
frame .vpane.lower.commarea.buffer
frame .vpane.lower.commarea.buffer.header
set ui_comm .vpane.lower.commarea.buffer.t
set ui_coml .vpane.lower.commarea.buffer.header.l
radiobutton .vpane.lower.commarea.buffer.header.new \
	-text {New Commit} \
	-command do_select_commit_type \
	-variable selected_commit_type \
	-value new \
	-font font_ui
lappend disable_on_lock \
	[list .vpane.lower.commarea.buffer.header.new conf -state]
radiobutton .vpane.lower.commarea.buffer.header.amend \
	-text {Amend Last Commit} \
	-command do_select_commit_type \
	-variable selected_commit_type \
	-value amend \
	-font font_ui
lappend disable_on_lock \
	[list .vpane.lower.commarea.buffer.header.amend conf -state]
label $ui_coml \
	-anchor w \
	-justify left \
	-font font_ui
proc trace_commit_type {varname args} {
	global ui_coml commit_type
	switch -glob -- $commit_type {
	initial       {set txt {Initial Commit Message:}}
	amend         {set txt {Amended Commit Message:}}
	amend-initial {set txt {Amended Initial Commit Message:}}
	amend-merge   {set txt {Amended Merge Commit Message:}}
	merge         {set txt {Merge Commit Message:}}
	*             {set txt {Commit Message:}}
	}
	$ui_coml conf -text $txt
}
trace add variable commit_type write trace_commit_type
pack $ui_coml -side left -fill x
pack .vpane.lower.commarea.buffer.header.amend -side right
pack .vpane.lower.commarea.buffer.header.new -side right

text $ui_comm -background white -borderwidth 1 \
	-undo true \
	-maxundo 20 \
	-autoseparators true \
	-relief sunken \
	-width 75 -height 9 -wrap none \
	-font font_diff \
	-yscrollcommand {.vpane.lower.commarea.buffer.sby set}
scrollbar .vpane.lower.commarea.buffer.sby \
	-command [list $ui_comm yview]
pack .vpane.lower.commarea.buffer.header -side top -fill x
pack .vpane.lower.commarea.buffer.sby -side right -fill y
pack $ui_comm -side left -fill y
pack .vpane.lower.commarea.buffer -side left -fill y

# -- Commit Message Buffer Context Menu
#
set ctxm .vpane.lower.commarea.buffer.ctxm
menu $ctxm -tearoff 0
$ctxm add command \
	-label {Cut} \
	-font font_ui \
	-command {tk_textCut $ui_comm}
$ctxm add command \
	-label {Copy} \
	-font font_ui \
	-command {tk_textCopy $ui_comm}
$ctxm add command \
	-label {Paste} \
	-font font_ui \
	-command {tk_textPaste $ui_comm}
$ctxm add command \
	-label {Delete} \
	-font font_ui \
	-command {$ui_comm delete sel.first sel.last}
$ctxm add separator
$ctxm add command \
	-label {Select All} \
	-font font_ui \
	-command {focus $ui_comm;$ui_comm tag add sel 0.0 end}
$ctxm add command \
	-label {Copy All} \
	-font font_ui \
	-command {
		$ui_comm tag add sel 0.0 end
		tk_textCopy $ui_comm
		$ui_comm tag remove sel 0.0 end
	}
$ctxm add separator
$ctxm add command \
	-label {Sign Off} \
	-font font_ui \
	-command do_signoff
bind_button3 $ui_comm "tk_popup $ctxm %X %Y"

# -- Diff Header
#
proc trace_current_diff_path {varname args} {
	global current_diff_path diff_actions file_states
	if {$current_diff_path eq {}} {
		set s {}
		set f {}
		set p {}
		set o disabled
	} else {
		set p $current_diff_path
		set s [mapdesc [lindex $file_states($p) 0] $p]
		set f {File:}
		set p [escape_path $p]
		set o normal
	}

	.vpane.lower.diff.header.status configure -text $s
	.vpane.lower.diff.header.file configure -text $f
	.vpane.lower.diff.header.path configure -text $p
	foreach w $diff_actions {
		uplevel #0 $w $o
	}
}
trace add variable current_diff_path write trace_current_diff_path

frame .vpane.lower.diff.header -background orange
label .vpane.lower.diff.header.status \
	-background orange \
	-width $max_status_desc \
	-anchor w \
	-justify left \
	-font font_ui
label .vpane.lower.diff.header.file \
	-background orange \
	-anchor w \
	-justify left \
	-font font_ui
label .vpane.lower.diff.header.path \
	-background orange \
	-anchor w \
	-justify left \
	-font font_ui
pack .vpane.lower.diff.header.status -side left
pack .vpane.lower.diff.header.file -side left
pack .vpane.lower.diff.header.path -fill x
set ctxm .vpane.lower.diff.header.ctxm
menu $ctxm -tearoff 0
$ctxm add command \
	-label {Copy} \
	-font font_ui \
	-command {
		clipboard clear
		clipboard append \
			-format STRING \
			-type STRING \
			-- $current_diff_path
	}
lappend diff_actions [list $ctxm entryconf [$ctxm index last] -state]
bind_button3 .vpane.lower.diff.header.path "tk_popup $ctxm %X %Y"

# -- Diff Body
#
frame .vpane.lower.diff.body
set ui_diff .vpane.lower.diff.body.t
text $ui_diff -background white -borderwidth 0 \
	-width 80 -height 15 -wrap none \
	-font font_diff \
	-xscrollcommand {.vpane.lower.diff.body.sbx set} \
	-yscrollcommand {.vpane.lower.diff.body.sby set} \
	-state disabled
scrollbar .vpane.lower.diff.body.sbx -orient horizontal \
	-command [list $ui_diff xview]
scrollbar .vpane.lower.diff.body.sby -orient vertical \
	-command [list $ui_diff yview]
pack .vpane.lower.diff.body.sbx -side bottom -fill x
pack .vpane.lower.diff.body.sby -side right -fill y
pack $ui_diff -side left -fill both -expand 1
pack .vpane.lower.diff.header -side top -fill x
pack .vpane.lower.diff.body -side bottom -fill both -expand 1

$ui_diff tag conf d_cr -elide true
$ui_diff tag conf d_@ -foreground blue -font font_diffbold
$ui_diff tag conf d_+ -foreground {#00a000}
$ui_diff tag conf d_- -foreground red

$ui_diff tag conf d_++ -foreground {#00a000}
$ui_diff tag conf d_-- -foreground red
$ui_diff tag conf d_+s \
	-foreground {#00a000} \
	-background {#e2effa}
$ui_diff tag conf d_-s \
	-foreground red \
	-background {#e2effa}
$ui_diff tag conf d_s+ \
	-foreground {#00a000} \
	-background ivory1
$ui_diff tag conf d_s- \
	-foreground red \
	-background ivory1

$ui_diff tag conf d<<<<<<< \
	-foreground orange \
	-font font_diffbold
$ui_diff tag conf d======= \
	-foreground orange \
	-font font_diffbold
$ui_diff tag conf d>>>>>>> \
	-foreground orange \
	-font font_diffbold

$ui_diff tag raise sel

# -- Diff Body Context Menu
#
set ctxm .vpane.lower.diff.body.ctxm
menu $ctxm -tearoff 0
$ctxm add command \
	-label {Refresh} \
	-font font_ui \
	-command reshow_diff
lappend diff_actions [list $ctxm entryconf [$ctxm index last] -state]
$ctxm add command \
	-label {Copy} \
	-font font_ui \
	-command {tk_textCopy $ui_diff}
lappend diff_actions [list $ctxm entryconf [$ctxm index last] -state]
$ctxm add command \
	-label {Select All} \
	-font font_ui \
	-command {focus $ui_diff;$ui_diff tag add sel 0.0 end}
lappend diff_actions [list $ctxm entryconf [$ctxm index last] -state]
$ctxm add command \
	-label {Copy All} \
	-font font_ui \
	-command {
		$ui_diff tag add sel 0.0 end
		tk_textCopy $ui_diff
		$ui_diff tag remove sel 0.0 end
	}
lappend diff_actions [list $ctxm entryconf [$ctxm index last] -state]
$ctxm add separator
$ctxm add command \
	-label {Apply/Reverse Hunk} \
	-font font_ui \
	-command {apply_hunk $cursorX $cursorY}
set ui_diff_applyhunk [$ctxm index last]
lappend diff_actions [list $ctxm entryconf $ui_diff_applyhunk -state]
$ctxm add separator
$ctxm add command \
	-label {Decrease Font Size} \
	-font font_ui \
	-command {incr_font_size font_diff -1}
lappend diff_actions [list $ctxm entryconf [$ctxm index last] -state]
$ctxm add command \
	-label {Increase Font Size} \
	-font font_ui \
	-command {incr_font_size font_diff 1}
lappend diff_actions [list $ctxm entryconf [$ctxm index last] -state]
$ctxm add separator
$ctxm add command \
	-label {Show Less Context} \
	-font font_ui \
	-command {if {$repo_config(gui.diffcontext) >= 2} {
		incr repo_config(gui.diffcontext) -1
		reshow_diff
	}}
lappend diff_actions [list $ctxm entryconf [$ctxm index last] -state]
$ctxm add command \
	-label {Show More Context} \
	-font font_ui \
	-command {
		incr repo_config(gui.diffcontext)
		reshow_diff
	}
lappend diff_actions [list $ctxm entryconf [$ctxm index last] -state]
$ctxm add separator
$ctxm add command -label {Options...} \
	-font font_ui \
	-command do_options
bind_button3 $ui_diff "
	set cursorX %x
	set cursorY %y
	if {\$ui_index eq \$current_diff_side} {
		$ctxm entryconf $ui_diff_applyhunk -label {Unstage Hunk From Commit}
	} else {
		$ctxm entryconf $ui_diff_applyhunk -label {Stage Hunk For Commit}
	}
	tk_popup $ctxm %X %Y
"
unset ui_diff_applyhunk

# -- Status Bar
#
label .status -textvariable ui_status_value \
	-anchor w \
	-justify left \
	-borderwidth 1 \
	-relief sunken \
	-font font_ui
pack .status -anchor w -side bottom -fill x

# -- Load geometry
#
catch {
set gm $repo_config(gui.geometry)
wm geometry . [lindex $gm 0]
.vpane sash place 0 \
	[lindex [.vpane sash coord 0] 0] \
	[lindex $gm 1]
.vpane.files sash place 0 \
	[lindex $gm 2] \
	[lindex [.vpane.files sash coord 0] 1]
unset gm
}

# -- Key Bindings
#
bind $ui_comm <$M1B-Key-Return> {do_commit;break}
bind $ui_comm <$M1B-Key-i> {do_add_all;break}
bind $ui_comm <$M1B-Key-I> {do_add_all;break}
bind $ui_comm <$M1B-Key-x> {tk_textCut %W;break}
bind $ui_comm <$M1B-Key-X> {tk_textCut %W;break}
bind $ui_comm <$M1B-Key-c> {tk_textCopy %W;break}
bind $ui_comm <$M1B-Key-C> {tk_textCopy %W;break}
bind $ui_comm <$M1B-Key-v> {tk_textPaste %W; %W see insert; break}
bind $ui_comm <$M1B-Key-V> {tk_textPaste %W; %W see insert; break}
bind $ui_comm <$M1B-Key-a> {%W tag add sel 0.0 end;break}
bind $ui_comm <$M1B-Key-A> {%W tag add sel 0.0 end;break}

bind $ui_diff <$M1B-Key-x> {tk_textCopy %W;break}
bind $ui_diff <$M1B-Key-X> {tk_textCopy %W;break}
bind $ui_diff <$M1B-Key-c> {tk_textCopy %W;break}
bind $ui_diff <$M1B-Key-C> {tk_textCopy %W;break}
bind $ui_diff <$M1B-Key-v> {break}
bind $ui_diff <$M1B-Key-V> {break}
bind $ui_diff <$M1B-Key-a> {%W tag add sel 0.0 end;break}
bind $ui_diff <$M1B-Key-A> {%W tag add sel 0.0 end;break}
bind $ui_diff <Key-Up>     {catch {%W yview scroll -1 units};break}
bind $ui_diff <Key-Down>   {catch {%W yview scroll  1 units};break}
bind $ui_diff <Key-Left>   {catch {%W xview scroll -1 units};break}
bind $ui_diff <Key-Right>  {catch {%W xview scroll  1 units};break}
bind $ui_diff <Button-1>   {focus %W}

if {[is_enabled branch]} {
	bind . <$M1B-Key-n> do_create_branch
	bind . <$M1B-Key-N> do_create_branch
}

bind all <Key-F5> do_rescan
bind all <$M1B-Key-r> do_rescan
bind all <$M1B-Key-R> do_rescan
bind .   <$M1B-Key-s> do_signoff
bind .   <$M1B-Key-S> do_signoff
bind .   <$M1B-Key-i> do_add_all
bind .   <$M1B-Key-I> do_add_all
bind .   <$M1B-Key-Return> do_commit
foreach i [list $ui_index $ui_workdir] {
	bind $i <Button-1>       "toggle_or_diff         $i %x %y; break"
	bind $i <$M1B-Button-1>  "add_one_to_selection   $i %x %y; break"
	bind $i <Shift-Button-1> "add_range_to_selection $i %x %y; break"
}
unset i

set file_lists($ui_index) [list]
set file_lists($ui_workdir) [list]

wm title . "[appname] ([file normalize [file dirname [gitdir]]])"
focus -force $ui_comm

# -- Warn the user about environmental problems.  Cygwin's Tcl
#    does *not* pass its env array onto any processes it spawns.
#    This means that git processes get none of our environment.
#
if {[is_Cygwin]} {
	set ignored_env 0
	set suggest_user {}
	set msg "Possible environment issues exist.

The following environment variables are probably
going to be ignored by any Git subprocess run
by [appname]:

"
	foreach name [array names env] {
		switch -regexp -- $name {
		{^GIT_INDEX_FILE$} -
		{^GIT_OBJECT_DIRECTORY$} -
		{^GIT_ALTERNATE_OBJECT_DIRECTORIES$} -
		{^GIT_DIFF_OPTS$} -
		{^GIT_EXTERNAL_DIFF$} -
		{^GIT_PAGER$} -
		{^GIT_TRACE$} -
		{^GIT_CONFIG$} -
		{^GIT_CONFIG_LOCAL$} -
		{^GIT_(AUTHOR|COMMITTER)_DATE$} {
			append msg " - $name\n"
			incr ignored_env
		}
		{^GIT_(AUTHOR|COMMITTER)_(NAME|EMAIL)$} {
			append msg " - $name\n"
			incr ignored_env
			set suggest_user $name
		}
		}
	}
	if {$ignored_env > 0} {
		append msg "
This is due to a known issue with the
Tcl binary distributed by Cygwin."

		if {$suggest_user ne {}} {
			append msg "

A good replacement for $suggest_user
is placing values for the user.name and
user.email settings into your personal
~/.gitconfig file.
"
		}
		warn_popup $msg
	}
	unset ignored_env msg suggest_user name
}

# -- Only initialize complex UI if we are going to stay running.
#
if {[is_enabled transport]} {
	load_all_remotes
	load_all_heads

	populate_branch_menu
	populate_fetch_menu
	populate_push_menu
}

# -- Only suggest a gc run if we are going to stay running.
#
if {[is_enabled multicommit]} {
	set object_limit 2000
	if {[is_Windows]} {set object_limit 200}
	regexp {^([0-9]+) objects,} [git count-objects] _junk objects_current
	if {$objects_current >= $object_limit} {
		if {[ask_popup \
			"This repository currently has $objects_current loose objects.

To maintain optimal performance it is strongly
recommended that you compress the database
when more than $object_limit loose objects exist.

Compress the database now?"] eq yes} {
			do_gc
		}
	}
	unset object_limit _junk objects_current
}

lock_index begin-read
after 1 do_rescan
