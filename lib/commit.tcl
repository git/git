# git-gui misc. commit reading/writing support
# Copyright (C) 2006, 2007 Shawn Pearce

proc load_last_commit {} {
	global HEAD PARENT MERGE_HEAD commit_type ui_comm
	global repo_config

	if {[llength $PARENT] == 0} {
		error_popup {There is nothing to amend.

You are about to create the initial commit.  There is no commit before this to amend.
}
		return
	}

	repository_state curType curHEAD curMERGE_HEAD
	if {$curType eq {merge}} {
		error_popup {Cannot amend while merging.

You are currently in the middle of a merge that has not been fully completed.  You cannot amend the prior commit unless you first abort the current merge activity.
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
			set msg [encoding convertfrom $enc [read $fd]]
			set msg [string trim $msg]
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

proc create_new_commit {} {
	global commit_type ui_comm

	set commit_type normal
	$ui_comm delete 0.0 end
	$ui_comm edit reset
	$ui_comm edit modified false
	rescan {set ui_status_value {Ready.}}
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

Another Git program has modified this repository since the last scan.  A rescan must be performed before another commit can be created.

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

File [short_path $path] has merge conflicts.  You must resolve them and add the file before committing.
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
	if {!$files_ready && ![string match *merge $curType]} {
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

	# -- Verify this wasn't an empty change.
	#
	if {$commit_type eq {normal}} {
		set old_tree [git rev-parse "$PARENT^{tree}"]
		if {$tree_id eq $old_tree} {
			info_popup {No changes to commit.

No files were modified by this commit and it was not a merge commit.

A rescan will be automatically started now.
}
			unlock_index
			rescan {set ui_status_value {No changes to commit.}}
			return
		}
	}

	# -- Build the message.
	#
	set msg_p [gitdir COMMIT_EDITMSG]
	set msg_wt [open $msg_p w]
	if {[catch {set enc $repo_config(i18n.commitencoding)}]} {
		set enc utf-8
	}
	fconfigure $msg_wt -encoding binary -translation binary
	puts -nonewline $msg_wt [encoding convertto $enc $msg]
	close $msg_wt

	# -- Create the commit.
	#
	set cmd [list commit-tree $tree_id]
	foreach p [concat $PARENT $MERGE_HEAD] {
		lappend cmd -p $p
	}
	lappend cmd <$msg_p
	if {[catch {set cmt_id [eval git $cmd]} err]} {
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
		set subject [string range $msg 0 [expr {$i - 1}]]
	} else {
		set subject $msg
	}
	append reflogm {: } $subject
	if {[catch {
			git update-ref -m $reflogm HEAD $cmt_id $curHEAD
		} err]} {
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
		"Created commit [string range $cmt_id 0 7]: $subject"
}
