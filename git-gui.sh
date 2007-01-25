#!/bin/sh
# Tcl ignores the next line -*- tcl -*- \
exec wish "$0" -- "$@"

set appvers {@@GIT_VERSION@@}
set copyright {
Copyright © 2006, 2007 Shawn Pearce, Paul Mackerras.

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

######################################################################
##
## read only globals

set _appname [lindex [file split $argv0] end]
set _gitdir {}
set _reponame {}

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

proc reponame {} {
	global _reponame
	return $_reponame
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

proc load_config {include_global} {
	global repo_config global_config default_config

	array unset global_config
	if {$include_global} {
		catch {
			set fd_rc [open "| git repo-config --global --list" r]
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
		set fd_rc [open "| git repo-config --list" r]
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
				catch {exec git repo-config --global --unset $name}
			} else {
				regsub -all "\[{}\]" $value {"} value
				exec git repo-config --global $name $value
			}
			set global_config($name) $value
			if {$value eq $repo_config($name)} {
				catch {exec git repo-config --unset $name}
				set repo_config($name) $value
			}
		}
	}

	foreach name [array names default_config] {
		set value $repo_config_new($name)
		if {$value ne $repo_config($name)} {
			if {$value eq $global_config($name)} {
				catch {exec git repo-config --unset $name}
			} else {
				regsub -all "\[{}\]" $value {"} value
				exec git repo-config $name $value
			}
			set repo_config($name) $value
		}
	}
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

proc info_popup {msg} {
	set title [appname]
	if {[reponame] ne {}} {
		append title " ([reponame])"
	}
	tk_messageBox \
		-parent . \
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
## repository setup

if {   [catch {set _gitdir $env(GIT_DIR)}]
	&& [catch {set _gitdir [exec git rev-parse --git-dir]} err]} {
	catch {wm withdraw .}
	error_popup "Cannot find the git directory:\n\n$err"
	exit 1
}
if {![file isdirectory $_gitdir]} {
	catch {wm withdraw .}
	error_popup "Git directory not found:\n\n$_gitdir"
	exit 1
}
if {[lindex [file split $_gitdir] end] ne {.git}} {
	catch {wm withdraw .}
	error_popup "Cannot use funny .git directory:\n\n$gitdir"
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

set single_commit 0
if {[appname] eq {git-citool}} {
	set single_commit 1
}

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

	if {[catch {set current_branch [exec git symbolic-ref HEAD]}]} {
		set current_branch {}
	} else {
		regsub ^refs/((heads|tags|remotes)/)? \
			$current_branch \
			{} \
			current_branch
	}

	if {[catch {set hd [exec git rev-parse --verify HEAD]}]} {
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
		set empty_tree [exec git mktree << {}]
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
	global ui_diff current_diff_path ui_index ui_workdir

	$ui_diff conf -state normal
	$ui_diff delete 0.0 end
	$ui_diff conf -state disabled

	set current_diff_path {}

	$ui_index tag remove in_diff 0.0 end
	$ui_workdir tag remove in_diff 0.0 end
}

proc reshow_diff {} {
	global ui_status_value file_states file_lists
	global current_diff_path current_diff_side

	set p $current_diff_path
	if {$p eq {}
		|| $current_diff_side eq {}
		|| [catch {set s $file_states($p)}]
		|| [lsearch -sorted $file_lists($current_diff_side) $p] == -1} {
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
	global current_diff_path current_diff_side

	if {$diff_active || ![lock_index read]} return

	clear_diff
	if {$w eq {} || $lno == {}} {
		foreach w [array names file_lists] {
			set lno [lsearch -sorted $file_lists($w) $path]
			if {$lno >= 0} {
				incr lno
				break
			}
		}
	}
	if {$w ne {} && $lno >= 1} {
		$w tag add in_diff $lno.0 [expr {$lno + 1}].0
	}

	set s $file_states($path)
	set m [lindex $s 0]
	set is_3way_diff 0
	set diff_active 1
	set current_diff_path $path
	set current_diff_side $w
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

	fconfigure $fd -blocking 0 -translation auto
	fileevent $fd readable [list read_diff $fd]
}

proc read_diff {fd} {
	global ui_diff ui_status_value is_3way_diff diff_active

	$ui_diff conf -state normal
	while {[gets $fd line] >= 0} {
		# -- Cleanup uninteresting diff header lines.
		#
		if {[string match {diff --git *}      $line]} continue
		if {[string match {diff --cc *}       $line]} continue
		if {[string match {diff --combined *} $line]} continue
		if {[string match {--- *}             $line]} continue
		if {[string match {+++ *}             $line]} continue
		if {$line eq {deleted file mode 120000}} {
			set line "deleted symlink"
		}

		# -- Automatically detect if this is a 3 way diff.
		#
		if {[string match {@@@ *} $line]} {set is_3way_diff 1}

		if {[string match {index *} $line]
			|| [string match {mode *} $line]
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
		if {[catch {set me [exec git var GIT_COMMITTER_IDENT]} err]} {
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

	if {![lock_index update]} return
	if {[committer_ident] eq {}} return

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
	if {[is_Windows] && [file isfile $pchook]} {
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
	global single_commit all_heads current_branch
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

	# -- Make sure our current branch exists.
	#
	if {$commit_type eq {initial}} {
		lappend all_heads $current_branch
		set all_heads [lsort -unique $all_heads]
		populate_branch_menu
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
		catch {exec git rerere}
	}

	# -- Run the post-commit hook.
	#
	set pchook [gitdir hooks post-commit]
	if {[is_Windows] && [file isfile $pchook]} {
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

	if {$single_commit} do_quit

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
## fetch pull push

proc fetch_from {remote} {
	set w [new_console "fetch $remote" \
		"Fetching new changes from $remote"]
	set cmd [list git fetch]
	lappend cmd $remote
	console_exec $w $cmd
}

proc pull_remote {remote branch} {
	global HEAD commit_type file_states repo_config

	if {![lock_index update]} return

	# -- Our in memory state should match the repository.
	#
	repository_state curType curHEAD curMERGE_HEAD
	if {$commit_type ne $curType || $HEAD ne $curHEAD} {
		info_popup {Last scanned state does not match repository state.

Another Git program has modified this repository
since the last scan.  A rescan must be performed
before a pull operation can be started.

The rescan will be automatically started now.
}
		unlock_index
		rescan {set ui_status_value {Ready.}}
		return
	}

	# -- No differences should exist before a pull.
	#
	if {[array size file_states] != 0} {
		error_popup {Uncommitted but modified files are present.

You should not perform a pull with unmodified
files in your working directory as Git will be
unable to recover from an incorrect merge.

You should commit or revert all changes before
starting a pull operation.
}
		unlock_index
		return
	}

	set w [new_console "pull $remote $branch" \
		"Pulling new changes from branch $branch in $remote"]
	set cmd [list git pull]
	if {$repo_config(gui.pullsummary) eq {false}} {
		lappend cmd --no-summary
	}
	lappend cmd $remote
	lappend cmd $branch
	console_exec $w $cmd [list post_pull_remote $remote $branch]
}

proc post_pull_remote {remote branch success} {
	global HEAD PARENT MERGE_HEAD commit_type selected_commit_type
	global ui_status_value

	unlock_index
	if {$success} {
		repository_state commit_type HEAD MERGE_HEAD
		set PARENT $HEAD
		set selected_commit_type new
		set ui_status_value "Pulling $branch from $remote complete."
	} else {
		rescan [list set ui_status_value \
			"Conflicts detected while pulling $branch from $remote."]
	}
}

proc push_to {remote} {
	set w [new_console "push $remote" \
		"Pushing changes to $remote"]
	set cmd [list git push]
	lappend cmd $remote
	console_exec $w $cmd
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
		set lno [lsearch -sorted $file_lists($w) $path]
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
		set lno [lsearch -sorted $file_lists($w) $path]
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

	$m add separator
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

proc do_create_branch_action {w} {
	global all_heads null_sha1 repo_config
	global create_branch_checkout create_branch_revtype
	global create_branch_head create_branch_trackinghead

	set newbranch [string trim [$w.desc.name_t get 0.0 end]]
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
	if {![catch {exec git show-ref --verify -- "refs/heads/$newbranch"}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Branch '$newbranch' already exists."
		focus $w.desc.name_t
		return
	}
	if {[catch {exec git check-ref-format "heads/$newbranch"}]} {
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
	expression {set rev [string trim [$w.from.exp_t get 0.0 end]]}
	}
	if {[catch {set cmt [exec git rev-parse --verify "${rev}^0"]}]} {
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

trace add variable delete_branch_head write \
	[list radio_selector delete_branch_checktype head]
trace add variable delete_branch_trackinghead write \
	[list radio_selector delete_branch_checktype tracking]

proc do_create_branch {} {
	global all_heads current_branch repo_config
	global create_branch_checkout create_branch_revtype
	global create_branch_head create_branch_trackinghead

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
	text $w.desc.name_t \
		-borderwidth 1 \
		-relief sunken \
		-height 1 \
		-width 40 \
		-font font_ui
	$w.desc.name_t insert 0.0 $repo_config(gui.newbranchtemplate)
	grid $w.desc.name_l $w.desc.name_t -sticky we -padx {0 5}
	bind $w.desc.name_t <Shift-Key-Tab> {focus [tk_focusPrev %W];break}
	bind $w.desc.name_t <Key-Tab> {focus [tk_focusNext %W];break}
	bind $w.desc.name_t <Key-Return> "do_create_branch_action $w;break"
	bind $w.desc.name_t <Key> {
		if {{%K} ne {BackSpace}
			&& {%K} ne {Tab}
			&& {%K} ne {Escape}
			&& {%K} ne {Return}} {
			if {%k <= 32} break
			if {[string first %A {~^:?*[}] >= 0} break
		}
	}
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
	radiobutton $w.from.exp_r \
		-text {Revision Expression:} \
		-value expression \
		-variable create_branch_revtype \
		-font font_ui
	text $w.from.exp_t \
		-borderwidth 1 \
		-relief sunken \
		-height 1 \
		-width 50 \
		-font font_ui
	grid $w.from.exp_r $w.from.exp_t -sticky we -padx {0 5}
	bind $w.from.exp_t <Shift-Key-Tab> {focus [tk_focusPrev %W];break}
	bind $w.from.exp_t <Key-Tab> {focus [tk_focusNext %W];break}
	bind $w.from.exp_t <Key-Return> "do_create_branch_action $w;break"
	bind $w.from.exp_t <Key-space> break
	bind $w.from.exp_t <Key> {set create_branch_revtype expression}
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

	bind $w <Visibility> "grab $w; focus $w.desc.name_t"
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
	} elseif {[catch {set check_cmt [exec git rev-parse --verify "${check_rev}^0"]}]} {
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
		if {[catch {set o [exec git rev-parse --verify $b]}]} continue
		if {$check_cmt ne {}} {
			if {$b eq $check_rev} continue
			if {[catch {set m [exec git merge-base $o $check_cmt]}]} continue
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
		if {[catch {exec git update-ref -d "refs/heads/$b" $o} err]} {
			append failed " - $b: $err\n"
		} else {
			set x [lsearch -sorted $all_heads $b]
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
		-width 50 \
		-selectmode extended \
		-font font_ui
	foreach h $all_heads {
		if {$h ne $current_branch} {
			$w.list.l insert end $h
		}
	}
	pack $w.list.l -fill both -pady 5 -padx 5
	pack $w.list -fill both -pady 5 -padx 5

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
	if {[catch {exec git symbolic-ref HEAD "refs/heads/$new_branch"} err]} {
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

proc populate_fetch_menu {m} {
	global all_remotes repo_config

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

proc populate_push_menu {m} {
	global all_remotes repo_config

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
			$m add command \
				-label "Push to $r..." \
				-command [list push_to $r] \
				-font font_ui
		}
	}
}

proc populate_pull_menu {m} {
	global repo_config all_remotes disable_on_lock

	foreach remote $all_remotes {
		set rb_list [list]
		if {[array get repo_config remote.$remote.url] ne {}} {
			if {[array get repo_config remote.$remote.fetch] ne {}} {
				foreach line $repo_config(remote.$remote.fetch) {
					if {[regexp {^([^:]+):} $line line rb]} {
						lappend rb_list $rb
					}
				}
			}
		} else {
			catch {
				set fd [open [gitdir remotes $remote] r]
				while {[gets $fd line] >= 0} {
					if {[regexp {^Pull:[ \t]*([^:]+):} $line line rb]} {
						lappend rb_list $rb
					}
				}
				close $fd
			}
		}

		foreach rb $rb_list {
			regsub ^refs/heads/ $rb {} rb_short
			$m add command \
				-label "Branch $rb_short from $remote..." \
				-command [list pull_remote $remote $rb] \
				-font font_ui
			lappend disable_on_lock \
				[list $m entryconf [$m index last] -state]
		}
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

proc bind_button3 {w cmd} {
	bind $w <Any-Button-3> $cmd
	if {[is_MacOSX]} {
		bind $w <Control-Button-1> $cmd
	}
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

proc console_exec {w cmd {after {}}} {
	# -- Windows tosses the enviroment when we exec our child.
	#    But most users need that so we have to relogin. :-(
	#
	if {[is_Windows]} {
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
	global console_cr console_data

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
			if {![winfo exists $w]} {console_init $w}
			$w.m.s conf -background red -text {Error: Command Failed}
			$w.ok conf -state normal
			set ok 0
		} elseif {[winfo exists $w]} {
			$w.m.s conf -background green -text {Success}
			$w.ok conf -state normal
			set ok 1
		}
		array unset console_cr $w
		array unset console_data $w
		if {$after ne {}} {
			uplevel #0 $after $ok
		}
		return
	}
	fconfigure $fd -blocking 0
}

######################################################################
##
## ui commands

set starting_gitk_msg {Starting gitk... please wait...}

proc do_gitk {revs} {
	global ui_status_value starting_gitk_msg

	set cmd gitk
	if {$revs ne {}} {
		append cmd { }
		append cmd $revs
	}
	if {[is_Windows]} {
		set cmd "sh -c \"exec $cmd\""
	}
	append cmd { &}

	if {[catch {eval exec $cmd} err]} {
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
	pack $w.stat

	bind $w <Visibility> "grab $w; focus $w"
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Key-Return> [list destroy $w]
	wm title $w "[appname] ([reponame]): Database Statistics"
	tkwait window $w
}

proc do_gc {} {
	set w [new_console {gc} {Compressing the object database}]
	console_exec $w {git gc}
}

proc do_fsck_objects {} {
	set w [new_console {fsck-objects} \
		{Verifying the object database with fsck-objects}]
	set cmd [list git fsck-objects]
	lappend cmd --full
	lappend cmd --cache
	lappend cmd --strict
	console_exec $w $cmd
}

set is_quitting 0

proc do_quit {} {
	global ui_comm is_quitting repo_config commit_type

	if {$is_quitting} return
	set is_quitting 1

	# -- Stash our current commit buffer.
	#
	set save [gitdir GITGUI_MSG]
	set msg [string trim [$ui_comm get 0.0 end]]
	if {![string match amend* $commit_type]
		&& [$ui_comm edit modified]
		&& $msg ne {}} {
		catch {
			set fd [open $save w]
			puts $fd [string trim [$ui_comm get 0.0 end]]
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
		catch {exec git repo-config gui.geometry $cfg_geometry}
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
	pack $w.buttons.close -side right
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	label $w.desc \
		-text "[appname] - a commit creation tool for Git.
$copyright" \
		-padx 5 -pady 5 \
		-justify left \
		-anchor w \
		-borderwidth 1 \
		-relief solid \
		-font font_ui
	pack $w.desc -side top -fill x -padx 5 -pady 5

	set v {}
	append v "[appname] version $appvers\n"
	append v "[exec git version]\n"
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

	label $w.header -text "[appname] Options" \
		-font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.restore -text {Restore Defaults} \
		-font font_ui \
		-command do_restore_defaults
	pack $w.buttons.restore -side left
	button $w.buttons.save -text Save \
		-font font_ui \
		-command "
			catch {eval \[bind \[focus -displayof $w\] <FocusOut>\]}
			do_save_config $w
		"
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

	foreach option {
		{b pullsummary {Show Pull Summary}}
		{b trustmtime  {Trust File Modification Timestamps}}
		{i diffcontext {Number of Diff Context Lines}}
		{t newbranchtemplate {New Branch Name Template}}
		} {
		set type [lindex $option 0]
		set name [lindex $option 1]
		set text [lindex $option 2]
		foreach f {repo global} {
			switch $type {
			b {
				checkbutton $w.$f.$name -text $text \
					-variable ${f}_config_new(gui.$name) \
					-onvalue true \
					-offvalue false \
					-font font_ui
				pack $w.$f.$name -side top -anchor w
			}
			i {
				frame $w.$f.$name
				label $w.$f.$name.l -text "$text:" -font font_ui
				pack $w.$f.$name.l -side left -anchor w -fill x
				spinbox $w.$f.$name.v \
					-textvariable ${f}_config_new(gui.$name) \
					-from 1 -to 99 -increment 1 \
					-width 3 \
					-font font_ui
				bind $w.$f.$name.v <FocusIn> {%W selection range 0 end}
				pack $w.$f.$name.v -side right -anchor e -padx 5
				pack $w.$f.$name -side top -anchor w -fill x
			}
			t {
				frame $w.$f.$name
				label $w.$f.$name.l -text "$text:" -font font_ui
				text $w.$f.$name.v \
					-borderwidth 1 \
					-relief sunken \
					-height 1 \
					-width 20 \
					-font font_ui
				$w.$f.$name.v insert 0.0 [set ${f}_config_new(gui.$name)]
				bind $w.$f.$name.v <Shift-Key-Tab> {focus [tk_focusPrev %W];break}
				bind $w.$f.$name.v <Key-Tab> {focus [tk_focusNext %W];break}
				bind $w.$f.$name.v <Key-Return> break
				bind $w.$f.$name.v <FocusIn> "$w.$f.$name.v tag add sel 0.0 end"
				bind $w.$f.$name.v <FocusOut> "
					set ${f}_config_new(gui.$name) \
					\[string trim \[$w.$f.$name.v get 0.0 end\]\]
				"
				pack $w.$f.$name.l -side left -anchor w
				pack $w.$f.$name.v -side left -anchor w \
					-fill x -expand 1 \
					-padx 5
				pack $w.$f.$name -side top -anchor w -fill x
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
				set ep [file normalize [exec git --exec-path]]
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

set default_config(gui.trustmtime) false
set default_config(gui.pullsummary) true
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
## ui construction

# -- Menu Bar
#
menu .mbar -tearoff 0
.mbar add cascade -label Repository -menu .mbar.repository
.mbar add cascade -label Edit -menu .mbar.edit
if {!$single_commit} {
	.mbar add cascade -label Branch -menu .mbar.branch
}
.mbar add cascade -label Commit -menu .mbar.commit
if {!$single_commit} {
	.mbar add cascade -label Fetch -menu .mbar.fetch
	.mbar add cascade -label Pull -menu .mbar.pull
	.mbar add cascade -label Push -menu .mbar.push
}
. configure -menu .mbar

# -- Repository Menu
#
menu .mbar.repository
.mbar.repository add command \
	-label {Visualize Current Branch} \
	-command {do_gitk {}} \
	-font font_ui
if {![is_MacOSX]} {
	.mbar.repository add command \
		-label {Visualize All Branches} \
		-command {do_gitk {--all}} \
		-font font_ui
}
.mbar.repository add separator

if {!$single_commit} {
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

	if {[is_Windows]} {
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
if {!$single_commit} {
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

.mbar.commit add command -label {Add All To Commit} \
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

# -- Transport menus
#
if {!$single_commit} {
	menu .mbar.fetch
	menu .mbar.pull
	menu .mbar.push
}

if {[is_MacOSX]} {
	# -- Apple Menu (Mac OS X only)
	#
	.mbar add cascade -label Apple -menu .mbar.apple
	menu .mbar.apple

	.mbar.apple add command -label "About [appname]" \
		-command do_about \
		-font font_ui
	.mbar.apple add command -label "[appname] Options..." \
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

	# -- Help Menu
	#
	.mbar add cascade -label Help -menu .mbar.help
	menu .mbar.help

	.mbar.help add command -label "About [appname]" \
		-command do_about \
		-font font_ui
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

button .vpane.lower.commarea.buttons.incall -text {Add All} \
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
set current_diff_path {}
set diff_actions [list]
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
bind_button3 $ui_diff "tk_popup $ctxm %X %Y"

# -- Status Bar
#
set ui_status_value {Initializing...}
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

if {!$single_commit} {
	bind . <$M1B-Key-n> do_create_branch
	bind . <$M1B-Key-N> do_create_branch
}

bind .   <Destroy> do_quit
bind all <Key-F5> do_rescan
bind all <$M1B-Key-r> do_rescan
bind all <$M1B-Key-R> do_rescan
bind .   <$M1B-Key-s> do_signoff
bind .   <$M1B-Key-S> do_signoff
bind .   <$M1B-Key-i> do_add_all
bind .   <$M1B-Key-I> do_add_all
bind .   <$M1B-Key-Return> do_commit
bind all <$M1B-Key-q> do_quit
bind all <$M1B-Key-Q> do_quit
bind all <$M1B-Key-w> {destroy [winfo toplevel %W]}
bind all <$M1B-Key-W> {destroy [winfo toplevel %W]}
foreach i [list $ui_index $ui_workdir] {
	bind $i <Button-1>       "toggle_or_diff         $i %x %y; break"
	bind $i <$M1B-Button-1>  "add_one_to_selection   $i %x %y; break"
	bind $i <Shift-Button-1> "add_range_to_selection $i %x %y; break"
}
unset i

set file_lists($ui_index) [list]
set file_lists($ui_workdir) [list]

set HEAD {}
set PARENT {}
set MERGE_HEAD [list]
set commit_type {}
set empty_tree {}
set current_branch {}
set current_diff_path {}
set selected_commit_type new

wm title . "[appname] ([file normalize [file dirname [gitdir]]])"
focus -force $ui_comm

# -- Warn the user about environmental problems.  Cygwin's Tcl
#    does *not* pass its env array onto any processes it spawns.
#    This means that git processes get none of our environment.
#
if {[is_Windows]} {
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
if {!$single_commit} {
	load_all_remotes
	load_all_heads

	populate_branch_menu
	populate_fetch_menu .mbar.fetch
	populate_pull_menu .mbar.pull
	populate_push_menu .mbar.push
}

# -- Only suggest a gc run if we are going to stay running.
#
if {!$single_commit} {
	set object_limit 2000
	if {[is_Windows]} {set object_limit 200}
	regexp {^([0-9]+) objects,} [exec git count-objects] _junk objects_current
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
