# git-gui misc. commit reading/writing support
# Copyright (C) 2006, 2007 Shawn Pearce

proc load_last_commit {} {
	global HEAD PARENT MERGE_HEAD commit_type ui_comm
	global repo_config

	if {[llength $PARENT] == 0} {
		error_popup [mc "There is nothing to amend.

You are about to create the initial commit.  There is no commit before this to amend.
"]
		return
	}

	repository_state curType curHEAD curMERGE_HEAD
	if {$curType eq {merge}} {
		error_popup [mc "Cannot amend while merging.

You are currently in the middle of a merge that has not been fully completed.  You cannot amend the prior commit unless you first abort the current merge activity.
"]
		return
	}

	set msg {}
	set parents [list]
	if {[catch {
			set fd [git_read cat-file commit $curHEAD]
			fconfigure $fd -encoding binary -translation lf
			# By default commits are assumed to be in utf-8
			set enc utf-8
			while {[gets $fd line] > 0} {
				if {[string match {parent *} $line]} {
					lappend parents [string range $line 7 end]
				} elseif {[string match {encoding *} $line]} {
					set enc [string tolower [string range $line 9 end]]
				}
			}
			set msg [read $fd]
			close $fd

			set enc [tcl_encoding $enc]
			if {$enc ne {}} {
				set msg [encoding convertfrom $enc $msg]
			}
			set msg [string trim $msg]
		} err]} {
		error_popup [strcat [mc "Error loading commit data for amend:"] "\n\n$err"]
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
	rescan ui_ready
}

set GIT_COMMITTER_IDENT {}

proc committer_ident {} {
	global GIT_COMMITTER_IDENT

	if {$GIT_COMMITTER_IDENT eq {}} {
		if {[catch {set me [git var GIT_COMMITTER_IDENT]} err]} {
			error_popup [strcat [mc "Unable to obtain your identity:"] "\n\n$err"]
			return {}
		}
		if {![regexp {^(.*) [0-9]+ [-+0-9]+$} \
			$me me GIT_COMMITTER_IDENT]} {
			error_popup [strcat [mc "Invalid GIT_COMMITTER_IDENT:"] "\n\n$me"]
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
	rescan ui_ready
}

proc setup_commit_encoding {msg_wt {quiet 0}} {
	global repo_config

	if {[catch {set enc $repo_config(i18n.commitencoding)}]} {
		set enc utf-8
	}
	set use_enc [tcl_encoding $enc]
	if {$use_enc ne {}} {
		fconfigure $msg_wt -encoding $use_enc
	} else {
		if {!$quiet} {
			error_popup [mc "warning: Tcl does not support encoding '%s'." $enc]
		}
		fconfigure $msg_wt -encoding utf-8
	}
}

proc commit_tree {} {
	global HEAD commit_type file_states ui_comm repo_config
	global pch_error

	if {[committer_ident] eq {}} return
	if {![lock_index update]} return

	# -- Our in memory state should match the repository.
	#
	repository_state curType curHEAD curMERGE_HEAD
	if {[string match amend* $commit_type]
		&& $curType eq {normal}
		&& $curHEAD eq $HEAD} {
	} elseif {$commit_type ne $curType || $HEAD ne $curHEAD} {
		info_popup [mc "Last scanned state does not match repository state.

Another Git program has modified this repository since the last scan.  A rescan must be performed before another commit can be created.

The rescan will be automatically started now.
"]
		unlock_index
		rescan ui_ready
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
		T_ -
		M? {set files_ready 1}
		_U -
		U? {
			error_popup [mc "Unmerged files cannot be committed.

File %s has merge conflicts.  You must resolve them and stage the file before committing.
" [short_path $path]]
			unlock_index
			return
		}
		default {
			error_popup [mc "Unknown file state %s detected.

File %s cannot be committed by this program.
" [lindex $s 0] [short_path $path]]
		}
		}
	}
	if {!$files_ready && ![string match *merge $curType] && ![is_enabled nocommit]} {
		info_popup [mc "No changes to commit.

You must stage at least 1 file before you can commit.
"]
		unlock_index
		return
	}

	if {[is_enabled nocommitmsg]} { do_quit 0 }

	# -- A message is required.
	#
	set msg [string trim [$ui_comm get 1.0 end]]
	regsub -all -line {[ \t\r]+$} $msg {} msg
	if {$msg eq {}} {
		error_popup [mc "Please supply a commit message.

A good commit message has the following format:

- First line: Describe in one sentence what you did.
- Second line: Blank
- Remaining lines: Describe why this change is good.
"]
		unlock_index
		return
	}

	# -- Build the message file.
	#
	set msg_p [gitdir GITGUI_EDITMSG]
	set msg_wt [open $msg_p w]
	fconfigure $msg_wt -translation lf
	setup_commit_encoding $msg_wt
	puts $msg_wt $msg
	close $msg_wt

	if {[is_enabled nocommit]} { do_quit 0 }

	# -- Run the pre-commit hook.
	#
	set fd_ph [githook_read pre-commit]
	if {$fd_ph eq {}} {
		commit_commitmsg $curHEAD $msg_p
		return
	}

	ui_status [mc "Calling pre-commit hook..."]
	set pch_error {}
	fconfigure $fd_ph -blocking 0 -translation binary -eofchar {}
	fileevent $fd_ph readable \
		[list commit_prehook_wait $fd_ph $curHEAD $msg_p]
}

proc commit_prehook_wait {fd_ph curHEAD msg_p} {
	global pch_error

	append pch_error [read $fd_ph]
	fconfigure $fd_ph -blocking 1
	if {[eof $fd_ph]} {
		if {[catch {close $fd_ph}]} {
			catch {file delete $msg_p}
			ui_status [mc "Commit declined by pre-commit hook."]
			hook_failed_popup pre-commit $pch_error
			unlock_index
		} else {
			commit_commitmsg $curHEAD $msg_p
		}
		set pch_error {}
		return
	}
	fconfigure $fd_ph -blocking 0
}

proc commit_commitmsg {curHEAD msg_p} {
	global pch_error

	# -- Run the commit-msg hook.
	#
	set fd_ph [githook_read commit-msg $msg_p]
	if {$fd_ph eq {}} {
		commit_writetree $curHEAD $msg_p
		return
	}

	ui_status [mc "Calling commit-msg hook..."]
	set pch_error {}
	fconfigure $fd_ph -blocking 0 -translation binary -eofchar {}
	fileevent $fd_ph readable \
		[list commit_commitmsg_wait $fd_ph $curHEAD $msg_p]
}

proc commit_commitmsg_wait {fd_ph curHEAD msg_p} {
	global pch_error

	append pch_error [read $fd_ph]
	fconfigure $fd_ph -blocking 1
	if {[eof $fd_ph]} {
		if {[catch {close $fd_ph}]} {
			catch {file delete $msg_p}
			ui_status [mc "Commit declined by commit-msg hook."]
			hook_failed_popup commit-msg $pch_error
			unlock_index
		} else {
			commit_writetree $curHEAD $msg_p
		}
		set pch_error {}
		return
	}
	fconfigure $fd_ph -blocking 0
}

proc commit_writetree {curHEAD msg_p} {
	ui_status [mc "Committing changes..."]
	set fd_wt [git_read write-tree]
	fileevent $fd_wt readable \
		[list commit_committree $fd_wt $curHEAD $msg_p]
}

proc commit_committree {fd_wt curHEAD msg_p} {
	global HEAD PARENT MERGE_HEAD commit_type
	global current_branch
	global ui_comm selected_commit_type
	global file_states selected_paths rescan_active
	global repo_config

	gets $fd_wt tree_id
	if {[catch {close $fd_wt} err]} {
		catch {file delete $msg_p}
		error_popup [strcat [mc "write-tree failed:"] "\n\n$err"]
		ui_status [mc "Commit failed."]
		unlock_index
		return
	}

	# -- Verify this wasn't an empty change.
	#
	if {$commit_type eq {normal}} {
		set fd_ot [git_read cat-file commit $PARENT]
		fconfigure $fd_ot -encoding binary -translation lf
		set old_tree [gets $fd_ot]
		close $fd_ot

		if {[string equal -length 5 {tree } $old_tree]
			&& [string length $old_tree] == 45} {
			set old_tree [string range $old_tree 5 end]
		} else {
			error [mc "Commit %s appears to be corrupt" $PARENT]
		}

		if {$tree_id eq $old_tree} {
			catch {file delete $msg_p}
			info_popup [mc "No changes to commit.

No files were modified by this commit and it was not a merge commit.

A rescan will be automatically started now.
"]
			unlock_index
			rescan {ui_status [mc "No changes to commit."]}
			return
		}
	}

	# -- Create the commit.
	#
	set cmd [list commit-tree $tree_id]
	foreach p [concat $PARENT $MERGE_HEAD] {
		lappend cmd -p $p
	}
	lappend cmd <$msg_p
	if {[catch {set cmt_id [eval git $cmd]} err]} {
		catch {file delete $msg_p}
		error_popup [strcat [mc "commit-tree failed:"] "\n\n$err"]
		ui_status [mc "Commit failed."]
		unlock_index
		return
	}

	# -- Update the HEAD ref.
	#
	set reflogm commit
	if {$commit_type ne {normal}} {
		append reflogm " ($commit_type)"
	}
	set msg_fd [open $msg_p r]
	setup_commit_encoding $msg_fd 1
	gets $msg_fd subject
	close $msg_fd
	append reflogm {: } $subject
	if {[catch {
			git update-ref -m $reflogm HEAD $cmt_id $curHEAD
		} err]} {
		catch {file delete $msg_p}
		error_popup [strcat [mc "update-ref failed:"] "\n\n$err"]
		ui_status [mc "Commit failed."]
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
	if {[get_config rerere.enabled] eq {}} {
		set rerere [file isdirectory [gitdir rr-cache]]
	} else {
		set rerere [is_config_true rerere.enabled]
	}
	if {$rerere} {
		catch {git rerere}
	}

	# -- Run the post-commit hook.
	#
	set fd_ph [githook_read post-commit]
	if {$fd_ph ne {}} {
		global pch_error
		set pch_error {}
		fconfigure $fd_ph -blocking 0 -translation binary -eofchar {}
		fileevent $fd_ph readable \
			[list commit_postcommit_wait $fd_ph $cmt_id]
	}

	$ui_comm delete 0.0 end
	$ui_comm edit reset
	$ui_comm edit modified false
	if {$::GITGUI_BCK_exists} {
		catch {file delete [gitdir GITGUI_BCK]}
		set ::GITGUI_BCK_exists 0
	}

	if {[is_enabled singlecommit]} { do_quit 0 }

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
		T_ -
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
	ui_status [mc "Created commit %s: %s" [string range $cmt_id 0 7] $subject]
}

proc commit_postcommit_wait {fd_ph cmt_id} {
	global pch_error

	append pch_error [read $fd_ph]
	fconfigure $fd_ph -blocking 1
	if {[eof $fd_ph]} {
		if {[catch {close $fd_ph}]} {
			hook_failed_popup post-commit $pch_error 0
		}
		unset pch_error
		return
	}
	fconfigure $fd_ph -blocking 0
}
