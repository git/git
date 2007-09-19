# git-gui commit checkout support
# Copyright (C) 2007 Shawn Pearce

class checkout_op {

field w        {}; # our window (if we have one)
field w_cons   {}; # embedded console window object

field new_expr   ; # expression the user saw/thinks this is
field new_hash   ; # commit SHA-1 we are switching to
field new_ref    ; # ref we are updating/creating

field parent_w      .; # window that started us
field merge_type none; # type of merge to apply to existing branch
field merge_base   {}; # merge base if we have another ref involved
field fetch_spec   {}; # refetch tracking branch if used?
field checkout      1; # actually checkout the branch?
field create        0; # create the branch if it doesn't exist?

field reset_ok      0; # did the user agree to reset?
field fetch_ok      0; # did the fetch succeed?

field readtree_d   {}; # buffered output from read-tree
field update_old   {}; # was the update-ref call deferred?
field reflog_msg   {}; # log message for the update-ref call

constructor new {expr hash {ref {}}} {
	set new_expr $expr
	set new_hash $hash
	set new_ref  $ref

	return $this
}

method parent {path} {
	set parent_w [winfo toplevel $path]
}

method enable_merge {type} {
	set merge_type $type
}

method enable_fetch {spec} {
	set fetch_spec $spec
}

method enable_checkout {co} {
	set checkout $co
}

method enable_create {co} {
	set create $co
}

method run {} {
	if {$fetch_spec ne {}} {
		global M1B

		# We were asked to refresh a single tracking branch
		# before we get to work.  We should do that before we
		# consider any ref updating.
		#
		set fetch_ok 0
		set l_trck [lindex $fetch_spec 0]
		set remote [lindex $fetch_spec 1]
		set r_head [lindex $fetch_spec 2]
		regsub ^refs/heads/ $r_head {} r_name

		set cmd [list git fetch $remote]
		if {$l_trck ne {}} {
			lappend cmd +$r_head:$l_trck
		} else {
			lappend cmd $r_head
		}

		_toplevel $this {Refreshing Tracking Branch}
		set w_cons [::console::embed \
			$w.console \
			"Fetching $r_name from $remote"]
		pack $w.console -fill both -expand 1
		$w_cons exec $cmd [cb _finish_fetch]

		bind $w <$M1B-Key-w> break
		bind $w <$M1B-Key-W> break
		bind $w <Visibility> "
			[list grab $w]
			[list focus $w]
		"
		wm protocol $w WM_DELETE_WINDOW [cb _noop]
		tkwait window $w

		if {!$fetch_ok} {
			delete_this
			return 0
		}
	}

	if {$new_ref ne {}} {
		# If we have a ref we need to update it before we can
		# proceed with a checkout (if one was enabled).
		#
		if {![_update_ref $this]} {
			delete_this
			return 0
		}
	}

	if {$checkout} {
		_checkout $this
		return 1
	}

	delete_this
	return 1
}

method _noop {} {}

method _finish_fetch {ok} {
	if {$ok} {
		set l_trck [lindex $fetch_spec 0]
		if {$l_trck eq {}} {
			set l_trck FETCH_HEAD
		}
		if {[catch {set new_hash [git rev-parse --verify "$l_trck^0"]} err]} {
			set ok 0
			$w_cons insert "fatal: Cannot resolve $l_trck"
			$w_cons insert $err
		}
	}

	$w_cons done $ok
	set w_cons {}
	wm protocol $w WM_DELETE_WINDOW {}

	if {$ok} {
		destroy $w
		set w {}
	} else {
		button $w.close -text Close -command [list destroy $w]
		pack $w.close -side bottom -anchor e -padx 10 -pady 10
	}

	set fetch_ok $ok
}

method _update_ref {} {
	global null_sha1 current_branch

	set ref $new_ref
	set new $new_hash

	set is_current 0
	set rh refs/heads/
	set rn [string length $rh]
	if {[string equal -length $rn $rh $ref]} {
		set newbranch [string range $ref $rn end]
		if {$current_branch eq $newbranch} {
			set is_current 1
		}
	} else {
		set newbranch $ref
	}

	if {[catch {set cur [git rev-parse --verify "$ref^0"]}]} {
		# Assume it does not exist, and that is what the error was.
		#
		if {!$create} {
			_error $this "Branch '$newbranch' does not exist."
			return 0
		}

		set reflog_msg "branch: Created from $new_expr"
		set cur $null_sha1
	} elseif {$create && $merge_type eq {none}} {
		# We were told to create it, but not do a merge.
		# Bad.  Name shouldn't have existed.
		#
		_error $this "Branch '$newbranch' already exists."
		return 0
	} elseif {!$create && $merge_type eq {none}} {
		# We aren't creating, it exists and we don't merge.
		# We are probably just a simple branch switch.
		# Use whatever value we just read.
		#
		set new      $cur
		set new_hash $cur
	} elseif {$new eq $cur} {
		# No merge would be required, don't compute anything.
		#
	} else {
		catch {set merge_base [git merge-base $new $cur]}
		if {$merge_base eq $cur} {
			# The current branch is older.
			#
			set reflog_msg "merge $new_expr: Fast-forward"
		} else {
			switch -- $merge_type {
			ff {
				if {$merge_base eq $new} {
					# The current branch is actually newer.
					#
					set new $cur
					set new_hash $cur
				} else {
					_error $this "Branch '$newbranch' already exists.\n\nIt cannot fast-forward to $new_expr.\nA merge is required."
					return 0
				}
			}
			reset {
				# The current branch will lose things.
				#
				if {[_confirm_reset $this $cur]} {
					set reflog_msg "reset $new_expr"
				} else {
					return 0
				}
			}
			default {
				_error $this "Merge strategy '$merge_type' not supported."
				return 0
			}
			}
		}
	}

	if {$new ne $cur} {
		if {$is_current} {
			# No so fast.  We should defer this in case
			# we cannot update the working directory.
			#
			set update_old $cur
			return 1
		}

		if {[catch {
				git update-ref -m $reflog_msg $ref $new $cur
			} err]} {
			_error $this "Failed to update '$newbranch'.\n\n$err"
			return 0
		}
	}

	return 1
}

method _checkout {} {
	if {[lock_index checkout_op]} {
		after idle [cb _start_checkout]
	} else {
		_error $this "Staging area (index) is already locked."
		delete_this
	}
}

method _start_checkout {} {
	global HEAD commit_type

	# -- Our in memory state should match the repository.
	#
	repository_state curType curHEAD curMERGE_HEAD
	if {[string match amend* $commit_type]
		&& $curType eq {normal}
		&& $curHEAD eq $HEAD} {
	} elseif {$commit_type ne $curType || $HEAD ne $curHEAD} {
		info_popup {Last scanned state does not match repository state.

Another Git program has modified this repository since the last scan.  A rescan must be performed before the current branch can be changed.

The rescan will be automatically started now.
}
		unlock_index
		rescan ui_ready
		delete_this
		return
	}

	if {$curHEAD eq $new_hash} {
		_after_readtree $this
	} elseif {[is_config_true gui.trustmtime]} {
		_readtree $this
	} else {
		ui_status {Refreshing file status...}
		set fd [git_read update-index \
			-q \
			--unmerged \
			--ignore-missing \
			--refresh \
			]
		fconfigure $fd -blocking 0 -translation binary
		fileevent $fd readable [cb _refresh_wait $fd]
	}
}

method _refresh_wait {fd} {
	read $fd
	if {[eof $fd]} {
		close $fd
		_readtree $this
	}
}

method _name {} {
	if {$new_ref eq {}} {
		return [string range $new_hash 0 7]
	}

	set rh refs/heads/
	set rn [string length $rh]
	if {[string equal -length $rn $rh $new_ref]} {
		return [string range $new_ref $rn end]
	} else {
		return $new_ref
	}
}

method _readtree {} {
	global HEAD

	set readtree_d {}
	$::main_status start \
		"Updating working directory to '[_name $this]'..." \
		{files checked out}

	set fd [git_read --stderr read-tree \
		-m \
		-u \
		-v \
		--exclude-per-directory=.gitignore \
		$HEAD \
		$new_hash \
		]
	fconfigure $fd -blocking 0 -translation binary
	fileevent $fd readable [cb _readtree_wait $fd]
}

method _readtree_wait {fd} {
	global current_branch

	set buf [read $fd]
	$::main_status update_meter $buf
	append readtree_d $buf

	fconfigure $fd -blocking 1
	if {![eof $fd]} {
		fconfigure $fd -blocking 0
		return
	}

	if {[catch {close $fd}]} {
		set err $readtree_d
		regsub {^fatal: } $err {} err
		$::main_status stop "Aborted checkout of '[_name $this]' (file level merging is required)."
		warn_popup "File level merge required.

$err

Staying on branch '$current_branch'."
		unlock_index
		delete_this
		return
	}

	$::main_status stop
	_after_readtree $this
}

method _after_readtree {} {
	global selected_commit_type commit_type HEAD MERGE_HEAD PARENT
	global current_branch is_detached
	global ui_comm

	set name [_name $this]
	set log "checkout: moving"
	if {!$is_detached} {
		append log " from $current_branch"
	}

	# -- Move/create HEAD as a symbolic ref.  Core git does not
	#    even check for failure here, it Just Works(tm).  If it
	#    doesn't we are in some really ugly state that is difficult
	#    to recover from within git-gui.
	#
	set rh refs/heads/
	set rn [string length $rh]
	if {[string equal -length $rn $rh $new_ref]} {
		set new_branch [string range $new_ref $rn end]
		if {$is_detached || $current_branch ne $new_branch} {
			append log " to $new_branch"
			if {[catch {
					git symbolic-ref -m $log HEAD $new_ref
				} err]} {
				_fatal $this $err
			}
			set current_branch $new_branch
			set is_detached 0
		}
	} else {
		if {!$is_detached || $new_hash ne $HEAD} {
			append log " to $new_expr"
			if {[catch {
					_detach_HEAD $log $new_hash
				} err]} {
				_fatal $this $err
			}
		}
		set current_branch HEAD
		set is_detached 1
	}

	# -- We had to defer updating the branch itself until we
	#    knew the working directory would update.  So now we
	#    need to finish that work.  If it fails we're in big
	#    trouble.
	#
	if {$update_old ne {}} {
		if {[catch {
				git update-ref \
					-m $reflog_msg \
					$new_ref \
					$new_hash \
					$update_old
			} err]} {
			_fatal $this $err
		}
	}

	if {$is_detached} {
		info_popup "You are no longer on a local branch.

If you wanted to be on a branch, create one now starting from 'This Detached Checkout'."
	}

	# -- Update our repository state.  If we were previously in
	#    amend mode we need to toss the current buffer and do a
	#    full rescan to update our file lists.  If we weren't in
	#    amend mode our file lists are accurate and we can avoid
	#    the rescan.
	#
	unlock_index
	set selected_commit_type new
	if {[string match amend* $commit_type]} {
		$ui_comm delete 0.0 end
		$ui_comm edit reset
		$ui_comm edit modified false
		rescan [list ui_status "Checked out '$name'."]
	} else {
		repository_state commit_type HEAD MERGE_HEAD
		set PARENT $HEAD
		ui_status "Checked out '$name'."
	}
	delete_this
}

git-version proc _detach_HEAD {log new} {
	>= 1.5.3 {
		git update-ref --no-deref -m $log HEAD $new
	}
	default {
		set p [gitdir HEAD]
		file delete $p
		set fd [open $p w]
		fconfigure $fd -translation lf -encoding utf-8
		puts $fd $new
		close $fd
	}
}

method _confirm_reset {cur} {
	set reset_ok 0
	set name [_name $this]
	set gitk [list do_gitk [list $cur ^$new_hash]]

	_toplevel $this {Confirm Branch Reset}
	pack [label $w.msg1 \
		-anchor w \
		-justify left \
		-text "Resetting '$name' to $new_expr will lose the following commits:" \
		] -anchor w

	set list $w.list.l
	frame $w.list
	text $list \
		-font font_diff \
		-width 80 \
		-height 10 \
		-wrap none \
		-xscrollcommand [list $w.list.sbx set] \
		-yscrollcommand [list $w.list.sby set]
	scrollbar $w.list.sbx -orient h -command [list $list xview]
	scrollbar $w.list.sby -orient v -command [list $list yview]
	pack $w.list.sbx -fill x -side bottom
	pack $w.list.sby -fill y -side right
	pack $list -fill both -expand 1
	pack $w.list -fill both -expand 1 -padx 5 -pady 5

	pack [label $w.msg2 \
		-anchor w \
		-justify left \
		-text {Recovering lost commits may not be easy.} \
		]
	pack [label $w.msg3 \
		-anchor w \
		-justify left \
		-text "Reset '$name'?" \
		]

	frame $w.buttons
	button $w.buttons.visualize \
		-text Visualize \
		-command $gitk
	pack $w.buttons.visualize -side left
	button $w.buttons.reset \
		-text Reset \
		-command "
			set @reset_ok 1
			destroy $w
		"
	pack $w.buttons.reset -side right
	button $w.buttons.cancel \
		-default active \
		-text Cancel \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	set fd [git_read rev-list --pretty=oneline $cur ^$new_hash]
	while {[gets $fd line] > 0} {
		set abbr [string range $line 0 7]
		set subj [string range $line 41 end]
		$list insert end "$abbr  $subj\n"
	}
	close $fd
	$list configure -state disabled

	bind $w    <Key-v> $gitk
	bind $w <Visibility> "
		grab $w
		focus $w.buttons.cancel
	"
	bind $w <Key-Return> [list destroy $w]
	bind $w <Key-Escape> [list destroy $w]
	tkwait window $w
	return $reset_ok
}

method _error {msg} {
	if {[winfo ismapped $parent_w]} {
		set p $parent_w
	} else {
		set p .
	}

	tk_messageBox \
		-icon error \
		-type ok \
		-title [wm title $p] \
		-parent $p \
		-message $msg
}

method _toplevel {title} {
	regsub -all {::} $this {__} w
	set w .$w

	if {[winfo ismapped $parent_w]} {
		set p $parent_w
	} else {
		set p .
	}

	toplevel $w
	wm title $w $title
	wm geometry $w "+[winfo rootx $p]+[winfo rooty $p]"
}

method _fatal {err} {
	error_popup "Failed to set current branch.

This working directory is only partially switched.  We successfully updated your files, but failed to update an internal Git file.

This should not have occurred.  [appname] will now close and give up.

$err"
	exit 1
}

}
