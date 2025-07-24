# git-gui Git repository chooser
# Copyright (C) 2007 Shawn Pearce

class choose_repository {

field top
field w
field w_body      ; # Widget holding the center content
field w_next      ; # Next button
field w_quit      ; # Quit button
field o_cons      ; # Console object (if active)

field w_types     ; # List of type buttons in clone
field w_recentlist ; # Listbox containing recent repositories
field w_localpath  ; # Entry widget bound to local_path

field done              0 ; # Finished picking the repository?
field clone_ok      false ; # clone succeeeded
field local_path       {} ; # Where this repository is locally
field origin_url       {} ; # Where we are cloning from
field origin_name  origin ; # What we shall call 'origin'
field clone_type hardlink ; # Type of clone to construct
field recursive      true ; # Recursive cloning flag
field readtree_err        ; # Error output from read-tree (if any)
field sorted_recent       ; # recent repositories (sorted)

constructor pick {} {
	global M1T M1B

	if {[set maxrecent [get_config gui.maxrecentrepo]] eq {}} {
		set maxrecent 10
	}

	make_dialog top w
	wm title $top [mc "Git Gui"]

	if {$top eq {.}} {
		menu $w.mbar -tearoff 0
		$top configure -menu $w.mbar

		set m_repo $w.mbar.repository
		$w.mbar add cascade \
			-label [mc Repository] \
			-menu $m_repo
		menu $m_repo

		if {[is_MacOSX]} {
			$w.mbar add cascade -label Apple -menu .mbar.apple
			menu $w.mbar.apple
			$w.mbar.apple add command \
				-label [mc "About %s" [appname]] \
				-command do_about
			$w.mbar.apple add command \
				-label [mc "Show SSH Key"] \
				-command do_ssh_key
		} else {
			$w.mbar add cascade -label [mc Help] -menu $w.mbar.help
			menu $w.mbar.help
			$w.mbar.help add command \
				-label [mc "About %s" [appname]] \
				-command do_about
			$w.mbar.help add command \
				-label [mc "Show SSH Key"] \
				-command do_ssh_key
		}

		wm protocol $top WM_DELETE_WINDOW exit
		bind $top <$M1B-q> exit
		bind $top <$M1B-Q> exit
		bind $top <Key-Escape> exit
	} else {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
		bind $top <Key-Escape> [list destroy $top]
		set m_repo {}
	}

	pack [git_logo $w.git_logo] -side left -fill y -padx 10 -pady 10

	set w_body $w.body
	set opts $w_body.options
	ttk::frame $w_body
	text $opts \
		-cursor $::cursor_ptr \
		-relief flat \
		-background [get_bg_color $w_body] \
		-wrap none \
		-spacing1 5 \
		-width 50 \
		-height 3
	pack $opts -anchor w -fill x

	$opts tag conf link_new -foreground blue -underline 1
	$opts tag bind link_new <1> [cb _next new]
	$opts insert end [mc "Create New Repository"] link_new
	$opts insert end "\n"
	if {$m_repo ne {}} {
		$m_repo add command \
			-command [cb _next new] \
			-accelerator $M1T-N \
			-label [mc "New..."]
		bind $top <$M1B-n> [cb _next new]
		bind $top <$M1B-N> [cb _next new]
	}

	$opts tag conf link_clone -foreground blue -underline 1
	$opts tag bind link_clone <1> [cb _next clone]
	$opts insert end [mc "Clone Existing Repository"] link_clone
	$opts insert end "\n"
	if {$m_repo ne {}} {
		if {[tk windowingsystem] eq "win32"} {
			set key L
		} else {
			set key C
		}
		$m_repo add command \
			-command [cb _next clone] \
			-accelerator $M1T-$key \
			-label [mc "Clone..."]
		bind $top <$M1B-[string tolower $key]> [cb _next clone]
		bind $top <$M1B-[string toupper $key]> [cb _next clone]
	}

	$opts tag conf link_open -foreground blue -underline 1
	$opts tag bind link_open <1> [cb _next open]
	$opts insert end [mc "Open Existing Repository"] link_open
	$opts insert end "\n"
	if {$m_repo ne {}} {
		$m_repo add command \
			-command [cb _next open] \
			-accelerator $M1T-O \
			-label [mc "Open..."]
		bind $top <$M1B-o> [cb _next open]
		bind $top <$M1B-O> [cb _next open]
	}

	$opts conf -state disabled

	set sorted_recent [_get_recentrepos]
	if {[llength $sorted_recent] > 0} {
		if {$m_repo ne {}} {
			$m_repo add separator
			$m_repo add command \
				-state disabled \
				-label [mc "Recent Repositories"]
		}

	if {[set lenrecent [llength $sorted_recent]] < $maxrecent} {
		set lenrecent $maxrecent
	}

		ttk::label $w_body.space
		ttk::label $w_body.recentlabel \
			-anchor w \
			-text [mc "Open Recent Repository:"]
		set w_recentlist $w_body.recentlist
		text $w_recentlist \
			-cursor $::cursor_ptr \
			-relief flat \
			-background [get_bg_color $w_body.recentlabel] \
			-wrap none \
			-width 50 \
			-height $lenrecent
		$w_recentlist tag conf link \
			-foreground blue \
			-underline 1
		set home $::env(HOME)
		set home "[file normalize $home]/"
		set hlen [string length $home]
		foreach p $sorted_recent {
			set path $p
			if {[string equal -length $hlen $home $p]} {
				set p "~/[string range $p $hlen end]"
			}
			regsub -all "\n" $p "\\n" p
			$w_recentlist insert end $p link
			$w_recentlist insert end "\n"

			if {$m_repo ne {}} {
				$m_repo add command \
					-command [cb _open_recent_path $path] \
					-label "    $p"
			}
		}
		$w_recentlist conf -state disabled
		$w_recentlist tag bind link <1> [cb _open_recent %x,%y]
		pack $w_body.space -anchor w -fill x
		pack $w_body.recentlabel -anchor w -fill x
		pack $w_recentlist -anchor w -fill x
	}
	pack $w_body -fill x -padx 10 -pady 10

	ttk::frame $w.buttons
	set w_next $w.buttons.next
	set w_quit $w.buttons.quit
	ttk::button $w_quit \
		-text [mc "Quit"] \
		-command exit
	pack $w_quit -side right -padx 5
	pack $w.buttons -side bottom -fill x -padx 10 -pady 10

	if {$m_repo ne {}} {
		$m_repo add separator
		$m_repo add command \
			-label [mc Quit] \
			-command exit \
			-accelerator $M1T-Q
	}

	bind $top <Return> [cb _invoke_next]
	bind $top <Visibility> "
		[cb _center]
		grab $top
		focus $top
		bind $top <Visibility> {}
	"
	wm deiconify $top
	tkwait variable @done

	grab release $top
	if {$top eq {.}} {
		eval destroy [winfo children $top]
	}
}

method _center {} {
	set nx [winfo reqwidth $top]
	set ny [winfo reqheight $top]
	set rx [expr {([winfo screenwidth  $top] - $nx) / 3}]
	set ry [expr {([winfo screenheight $top] - $ny) / 3}]
	wm geometry $top [format {+%d+%d} $rx $ry]
}

method _invoke_next {} {
	if {[winfo exists $w_next]} {
		uplevel #0 [$w_next cget -command]
	}
}

proc _get_recentrepos {} {
	set recent [list]
	foreach p [lsort -unique [get_config gui.recentrepo]] {
		if {[_is_git [file join $p .git]]} {
			lappend recent $p
		} else {
			_unset_recentrepo $p
		}
	}
	return $recent
}

proc _unset_recentrepo {p} {
	regsub -all -- {([()\[\]{}\.^$+*?\\])} $p {\\\1} p
	catch {git config --global --unset-all gui.recentrepo "^$p\$"}
	load_config 1
}

proc _append_recentrepos {path} {
	set path [file normalize $path]
	set recent [get_config gui.recentrepo]

	if {[lindex $recent end] eq $path} {
		return
	}

	set i [lsearch $recent $path]
	if {$i >= 0} {
		_unset_recentrepo $path
	}

	git config --global --add gui.recentrepo $path
	load_config 1
	set recent [get_config gui.recentrepo]

	if {[set maxrecent [get_config gui.maxrecentrepo]] eq {}} {
		set maxrecent 10
	}

	while {[llength $recent] > $maxrecent} {
		_unset_recentrepo [lindex $recent 0]
		set recent [get_config gui.recentrepo]
	}
}

method _open_recent {xy} {
	set id [lindex [split [$w_recentlist index @$xy] .] 0]
	set local_path [lindex $sorted_recent [expr {$id - 1}]]
	_do_open2 $this
}

method _open_recent_path {p} {
	set local_path $p
	_do_open2 $this
}

method _next {action} {
	destroy $w_body
	if {![winfo exists $w_next]} {
		ttk::button $w_next -default active
		set pos -before
		if {[tk windowingsystem] eq "win32"} { set pos -after }
		pack $w_next -side right -padx 5 $pos $w_quit
	}
	_do_$action $this
}

method _write_local_path {args} {
	if {$local_path eq {}} {
		$w_next conf -state disabled
	} else {
		$w_next conf -state normal
	}
}

method _git_init {} {
	if {[catch {git init $local_path} err]} {
		error_popup [strcat \
			[mc "Failed to create repository %s:" $local_path] \
			"\n\n$err"]
		return 0
	}

	if {[catch {cd $local_path} err]} {
		error_popup [strcat \
			[mc "Failed to create repository %s:" $local_path] \
			"\n\n$err"]
		return 0
	}

	_append_recentrepos [pwd]
	set ::_gitdir .git
	set ::_prefix {}
	return 1
}

proc _is_git {path {outdir_var ""}} {
	if {$outdir_var ne ""} {
		upvar 1 $outdir_var outdir
	}
	if {[catch {set outdir [git rev-parse --resolve-git-dir $path]}]} {
		return 0
	}
	return 1
}

######################################################################
##
## Create New Repository

method _do_new {} {
	$w_next conf \
		-state disabled \
		-command [cb _do_new2] \
		-text [mc "Create"]

	ttk::frame $w_body
	ttk::label $w_body.h \
		-font font_uibold -anchor center \
		-text [mc "Create New Repository"]
	pack $w_body.h -side top -fill x -pady 10
	pack $w_body -fill x -padx 10

	ttk::frame $w_body.where
	ttk::label $w_body.where.l -text [mc "Directory:"]
	ttk::entry $w_body.where.t \
		-textvariable @local_path \
		-width 50
	ttk::button $w_body.where.b \
		-text [mc "Browse"] \
		-command [cb _new_local_path]
	set w_localpath $w_body.where.t

	grid $w_body.where.l $w_body.where.t $w_body.where.b -sticky ew
	pack $w_body.where -fill x

	grid columnconfigure $w_body.where 1 -weight 1

	trace add variable @local_path write [cb _write_local_path]
	bind $w_body.h <Destroy> [list trace remove variable @local_path write [cb _write_local_path]]
	update
	focus $w_body.where.t
}

method _new_local_path {} {
	if {$local_path ne {}} {
		set p [file dirname $local_path]
	} else {
		set p [pwd]
	}

	set p [tk_chooseDirectory \
		-initialdir $p \
		-parent $top \
		-title [mc "Git Repository"] \
		-mustexist false]
	if {$p eq {}} return

	set p [file normalize $p]
	if {![_new_ok $p]} {
		return
	}
	set local_path $p
	$w_localpath icursor end
}

method _do_new2 {} {
	if {![_new_ok $local_path]} {
		return
	}
	if {![_git_init $this]} {
		return
	}
	set done 1
}

proc _new_ok {p} {
	if {[file isdirectory $p]} {
		if {[_is_git [file join $p .git]]} {
			error_popup [mc "Directory %s already exists." $p]
			return 0
		}
	} elseif {[file exists $p]} {
		error_popup [mc "File %s already exists." $p]
		return 0
	}
	return 1
}

######################################################################
##
## Clone Existing Repository

method _do_clone {} {
	$w_next conf \
		-state disabled \
		-command [cb _do_clone2] \
		-text [mc "Clone"]

	ttk::frame $w_body
	ttk::label $w_body.h \
		-font font_uibold -anchor center \
		-text [mc "Clone Existing Repository"]
	pack $w_body.h -side top -fill x -pady 10
	pack $w_body -fill x -padx 10

	set args $w_body.args
	ttk::frame $w_body.args
	pack $args -fill both

	ttk::label $args.origin_l -text [mc "Source Location:"]
	ttk::entry $args.origin_t \
		-textvariable @origin_url \
		-width 50
	ttk::button $args.origin_b \
		-text [mc "Browse"] \
		-command [cb _open_origin]
	grid $args.origin_l $args.origin_t $args.origin_b -sticky ew

	ttk::label $args.where_l -text [mc "Target Directory:"]
	ttk::entry $args.where_t \
		-textvariable @local_path \
		-width 50
	ttk::button $args.where_b \
		-text [mc "Browse"] \
		-command [cb _new_local_path]
	grid $args.where_l $args.where_t $args.where_b -sticky ew
	set w_localpath $args.where_t

	ttk::label $args.type_l -text [mc "Clone Type:"]
	ttk::frame $args.type_f
	set w_types [list]
	lappend w_types [ttk::radiobutton $args.type_f.hardlink \
		-state disabled \
		-text [mc "Standard (Fast, Semi-Redundant, Hardlinks)"] \
		-variable @clone_type \
		-value hardlink]
	lappend w_types [ttk::radiobutton $args.type_f.full \
		-state disabled \
		-text [mc "Full Copy (Slower, Redundant Backup)"] \
		-variable @clone_type \
		-value full]
	lappend w_types [ttk::radiobutton $args.type_f.shared \
		-state disabled \
		-text [mc "Shared (Fastest, Not Recommended, No Backup)"] \
		-variable @clone_type \
		-value shared]
	foreach r $w_types {
		pack $r -anchor w
	}
	ttk::checkbutton $args.type_f.recursive \
		-text [mc "Recursively clone submodules too"] \
		-variable @recursive \
		-onvalue true -offvalue false
	pack $args.type_f.recursive -anchor w
	grid $args.type_l $args.type_f -sticky new

	grid columnconfigure $args 1 -weight 1

	trace add variable @local_path write [cb _update_clone]
	trace add variable @origin_url write [cb _update_clone]
	bind $w_body.h <Destroy> "
		[list trace remove variable @local_path write [cb _update_clone]]
		[list trace remove variable @origin_url write [cb _update_clone]]
	"
	update
	focus $args.origin_t
}

method _open_origin {} {
	if {$origin_url ne {} && [file isdirectory $origin_url]} {
		set p $origin_url
	} else {
		set p [pwd]
	}

	set p [tk_chooseDirectory \
		-initialdir $p \
		-parent $top \
		-title [mc "Git Repository"] \
		-mustexist true]
	if {$p eq {}} return

	set p [file normalize $p]
	if {![_is_git [file join $p .git]] && ![_is_git $p]} {
		error_popup [mc "Not a Git repository: %s" [file tail $p]]
		return
	}
	set origin_url $p
}

method _update_clone {args} {
	if {$local_path ne {} && $origin_url ne {}} {
		$w_next conf -state normal
	} else {
		$w_next conf -state disabled
	}

	if {$origin_url ne {} &&
		(  [_is_git [file join $origin_url .git]]
		|| [_is_git $origin_url])} {
		set e normal
		if {[[lindex $w_types 0] cget -state] eq {disabled}} {
			set clone_type hardlink
		}
	} else {
		set e disabled
		set clone_type full
	}

	foreach r $w_types {
		$r conf -state $e
	}
}

method _do_clone2 {} {
	if {[file isdirectory $origin_url]} {
		set origin_url [file normalize $origin_url]
		if {$clone_type eq {hardlink}} {
			# cannot use hardlinks if this is a linked worktree (.gitfile or git-new-workdir)
			if {[git -C $origin_url rev-parse --is-inside-work-tree] == {true}} {
				set islink 0
				set dotgit [file join $origin_url .git]
				if {[file isfile $dotgit]} {
					set islink 1
				} else {
					set objdir [file join $dotgit objects]
					if {[file exists $objdir] && [file type $objdir] == {link}} {
						set islink 1
					}
				}
				if {$islink} {
					info_popup [mc "Hardlinks are unavailable.  Falling back to copying."]
					set clone_type full
				}
			}
		}
	}

	if {$clone_type eq {hardlink} && ![file isdirectory $origin_url]} {
		error_popup [mc "Standard only available for local repository."]
		return
	}
	if {$clone_type eq {shared} && ![file isdirectory $origin_url]} {
		error_popup [mc "Shared only available for local repository."]
		return
	}

	set giturl $origin_url

	if {[file exists $local_path]} {
		error_popup [mc "Location %s already exists." $local_path]
		return
	}

	set clone_options {--progress}
	if {$recursive} {
		append clone_options { --recurse-submodules}
	}

	destroy $w_body $w_next

	switch -exact -- $clone_type {
		full {
			append clone_options { --no-hardlinks --no-local}
		}
		shared {
			append clone_options { --shared}
		}
	}

	if {[catch {
		set o_cons [console::embed \
			$w_body \
			[mc "Cloning from %s" $origin_url]]
		pack $w_body -fill both -expand 1 -padx 10
		$o_cons exec \
			[list git clone {*}$clone_options $origin_url $local_path] \
			[cb _do_clone2_done]
	} err]} {
		error_popup [strcat [mc "Clone failed."] "\n" $err]
		return
	}

	tkwait variable @done
	if {!$clone_ok} {
		error_popup [mc "Clone failed."]
		return
	}
}

method _do_clone2_done {ok} {
	$o_cons done $ok
	if {$ok} {
		if {[catch {
			cd $local_path
			set ::_gitdir .git
			set ::_prefix {}
			_append_recentrepos [pwd]
		} err]} {
			set ok 0
		}
	}
	if {!$ok} {
		set ::_gitdir {}
		set ::_prefix {}
	}
	set clone_ok $ok
	set done 1
}


######################################################################
##
## Open Existing Repository

method _do_open {} {
	$w_next conf \
		-state disabled \
		-command [cb _do_open2] \
		-text [mc "Open"]

	ttk::frame $w_body
	ttk::label $w_body.h \
		-font font_uibold -anchor center \
		-text [mc "Open Existing Repository"]
	pack $w_body.h -side top -fill x -pady 10
	pack $w_body -fill x -padx 10

	ttk::frame $w_body.where
	ttk::label $w_body.where.l -text [mc "Repository:"]
	ttk::entry $w_body.where.t \
		-textvariable @local_path \
		-width 50
	ttk::button $w_body.where.b \
		-text [mc "Browse"] \
		-command [cb _open_local_path]

	grid $w_body.where.l $w_body.where.t $w_body.where.b -sticky ew
	pack $w_body.where -fill x

	grid columnconfigure $w_body.where 1 -weight 1

	trace add variable @local_path write [cb _write_local_path]
	bind $w_body.h <Destroy> [list trace remove variable @local_path write [cb _write_local_path]]
	update
	focus $w_body.where.t
}

method _open_local_path {} {
	if {$local_path ne {}} {
		set p $local_path
	} else {
		set p [pwd]
	}

	set p [tk_chooseDirectory \
		-initialdir $p \
		-parent $top \
		-title [mc "Git Repository"] \
		-mustexist true]
	if {$p eq {}} return

	set p [file normalize $p]
	if {![_is_git [file join $p .git]]} {
		error_popup [mc "Not a Git repository: %s" [file tail $p]]
		return
	}
	set local_path $p
}

method _do_open2 {} {
	if {![_is_git [file join $local_path .git] actualgit]} {
		error_popup [mc "Not a Git repository: %s" [file tail $local_path]]
		return
	}

	if {[catch {cd $local_path} err]} {
		error_popup [strcat \
			[mc "Failed to open repository %s:" $local_path] \
			"\n\n$err"]
		return
	}

	_append_recentrepos [pwd]
	set ::_gitdir $actualgit
	set ::_prefix {}
	set done 1
}

}
