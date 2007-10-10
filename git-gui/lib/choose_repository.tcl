# git-gui Git repository chooser
# Copyright (C) 2007 Shawn Pearce

class choose_repository {

field top
field w
field w_body      ; # Widget holding the center content
field w_next      ; # Next button
field o_cons      ; # Console object (if active)
field w_types     ; # List of type buttons in clone

field action          new ; # What action are we going to perform?
field done              0 ; # Finished picking the repository?
field local_path       {} ; # Where this repository is locally
field origin_url       {} ; # Where we are cloning from
field origin_name  origin ; # What we shall call 'origin'
field clone_type hardlink ; # Type of clone to construct
field readtree_err        ; # Error output from read-tree (if any)

constructor pick {} {
	global M1T M1B

	make_toplevel top w
	wm title $top [mc "Git Gui"]

	if {$top eq {.}} {
		menu $w.mbar -tearoff 0
		$top configure -menu $w.mbar

		$w.mbar add cascade \
			-label [mc Repository] \
			-menu $w.mbar.repository
		menu $w.mbar.repository
		$w.mbar.repository add command \
			-label [mc Quit] \
			-command exit \
			-accelerator $M1T-Q

		if {[is_MacOSX]} {
			$w.mbar add cascade -label [mc Apple] -menu .mbar.apple
			menu $w.mbar.apple
			$w.mbar.apple add command \
				-label [mc "About %s" [appname]] \
				-command do_about
		} else {
			$w.mbar add cascade -label [mc Help] -menu $w.mbar.help
			menu $w.mbar.help
			$w.mbar.help add command \
				-label [mc "About %s" [appname]] \
				-command do_about
		}

		wm protocol $top WM_DELETE_WINDOW exit
		bind $top <$M1B-q> exit
		bind $top <$M1B-Q> exit
		bind $top <Key-Escape> exit
	} else {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
		bind $top <Key-Escape> [list destroy $top]
	}

	pack [git_logo $w.git_logo] -side left -fill y -padx 10 -pady 10

	set w_body $w.body
	frame $w_body
	radiobutton $w_body.new \
		-anchor w \
		-text [mc "Create New Repository"] \
		-variable @action \
		-value new
	radiobutton $w_body.clone \
		-anchor w \
		-text [mc "Clone Existing Repository"] \
		-variable @action \
		-value clone
	radiobutton $w_body.open \
		-anchor w \
		-text [mc "Open Existing Repository"] \
		-variable @action \
		-value open
	pack $w_body.new -anchor w -fill x
	pack $w_body.clone -anchor w -fill x
	pack $w_body.open -anchor w -fill x
	pack $w_body -fill x -padx 10 -pady 10

	frame $w.buttons
	set w_next $w.buttons.next
	button $w_next \
		-default active \
		-text [mc "Next >"] \
		-command [cb _next]
	pack $w_next -side right -padx 5
	button $w.buttons.quit \
		-text [mc "Quit"] \
		-command exit
	pack $w.buttons.quit -side right -padx 5
	pack $w.buttons -side bottom -fill x -padx 10 -pady 10

	bind $top <Return> [cb _invoke_next]
	bind $top <Visibility> "
		[cb _center]
		grab $top
		focus $top
		bind $top <Visibility> {}
	"
	wm deiconify $top
	tkwait variable @done

	if {$top eq {.}} {
		eval destroy [winfo children $top]
	}
}

proc _home {} {
	if {[catch {set h $::env(HOME)}]
		|| ![file isdirectory $h]} {
		set h .
	}
	return $h
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

method _next {} {
	destroy $w_body
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
	if {[file exists $local_path]} {
		error_popup [mc "Location %s already exists." $local_path]
		return 0
	}

	if {[catch {file mkdir $local_path} err]} {
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

	if {[catch {git init} err]} {
		error_popup [strcat \
			[mc "Failed to create repository %s:" $local_path] \
			"\n\n$err"]
		return 0
	}

	set ::_gitdir .git
	set ::_prefix {}
	return 1
}

proc _is_git {path} {
	if {[file exists [file join $path HEAD]]
	 && [file exists [file join $path objects]]
	 && [file exists [file join $path config]]} {
		return 1
	}
	return 0
}

######################################################################
##
## Create New Repository

method _do_new {} {
	$w_next conf \
		-state disabled \
		-command [cb _do_new2] \
		-text [mc "Create"]

	frame $w_body
	label $w_body.h \
		-font font_uibold \
		-text [mc "Create New Repository"]
	pack $w_body.h -side top -fill x -pady 10
	pack $w_body -fill x -padx 10

	frame $w_body.where
	label $w_body.where.l -text [mc "Directory:"]
	entry $w_body.where.t \
		-textvariable @local_path \
		-font font_diff \
		-width 50
	button $w_body.where.b \
		-text [mc "Browse"] \
		-command [cb _new_local_path]

	pack $w_body.where.b -side right
	pack $w_body.where.l -side left
	pack $w_body.where.t -fill x
	pack $w_body.where -fill x

	trace add variable @local_path write [cb _write_local_path]
	update
	focus $w_body.where.t
}

method _new_local_path {} {
	if {$local_path ne {}} {
		set p [file dirname $local_path]
	} else {
		set p [_home]
	}

	set p [tk_chooseDirectory \
		-initialdir $p \
		-parent $top \
		-title [mc "Git Repository"] \
		-mustexist false]
	if {$p eq {}} return

	set p [file normalize $p]
	if {[file isdirectory $p]} {
		foreach i [glob \
			-directory $p \
			-tails \
			-nocomplain \
			* .*] {
			switch -- $i {
			 . continue
			.. continue
			default {
				error_popup [mc "Directory %s already exists." $p]
				return
			}
			}
		}
		if {[catch {file delete $p} err]} {
			error_popup [strcat \
				[mc "Directory %s already exists." $p] \
				"\n\n$err"]
			return
		}
	} elseif {[file exists $p]} {
		error_popup [mc "File %s already exists." $p]
		return
	}
	set local_path $p
}

method _do_new2 {} {
	if {![_git_init $this]} {
		return
	}
	set done 1
}

######################################################################
##
## Clone Existing Repository

method _do_clone {} {
	$w_next conf \
		-state disabled \
		-command [cb _do_clone2] \
		-text [mc "Clone"]

	frame $w_body
	label $w_body.h \
		-font font_uibold \
		-text [mc "Clone Existing Repository"]
	pack $w_body.h -side top -fill x -pady 10
	pack $w_body -fill x -padx 10

	set args $w_body.args
	frame $w_body.args
	pack $args -fill both

	label $args.origin_l -text [mc "URL:"]
	entry $args.origin_t \
		-textvariable @origin_url \
		-font font_diff \
		-width 50
	button $args.origin_b \
		-text [mc "Browse"] \
		-command [cb _open_origin]
	grid $args.origin_l $args.origin_t $args.origin_b -sticky ew

	label $args.where_l -text [mc "Directory:"]
	entry $args.where_t \
		-textvariable @local_path \
		-font font_diff \
		-width 50
	button $args.where_b \
		-text [mc "Browse"] \
		-command [cb _new_local_path]
	grid $args.where_l $args.where_t $args.where_b -sticky ew

	label $args.type_l -text [mc "Clone Type:"]
	frame $args.type_f
	set w_types [list]
	lappend w_types [radiobutton $args.type_f.hardlink \
		-state disabled \
		-anchor w \
		-text [mc "Standard (Fast, Semi-Redundant, Hardlinks)"] \
		-variable @clone_type \
		-value hardlink]
	lappend w_types [radiobutton $args.type_f.full \
		-state disabled \
		-anchor w \
		-text [mc "Full Copy (Slower, Redundant Backup)"] \
		-variable @clone_type \
		-value full]
	lappend w_types [radiobutton $args.type_f.shared \
		-state disabled \
		-anchor w \
		-text [mc "Shared (Fastest, Not Recommended, No Backup)"] \
		-variable @clone_type \
		-value shared]
	foreach r $w_types {
		pack $r -anchor w
	}
	grid $args.type_l $args.type_f -sticky new

	grid columnconfigure $args 1 -weight 1

	trace add variable @local_path write [cb _update_clone]
	trace add variable @origin_url write [cb _update_clone]
	update
	focus $args.origin_t
}

method _open_origin {} {
	if {$origin_url ne {} && [file isdirectory $origin_url]} {
		set p $origin_url
	} else {
		set p [_home]
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
	}

	if {$clone_type eq {hardlink} && ![file isdirectory $origin_url]} {
		error_popup [mc "Standard only available for local repository."]
		return
	}
	if {$clone_type eq {shared} && ![file isdirectory $origin_url]} {
		error_popup [mc "Shared only available for local repository."]
		return
	}

	if {$clone_type eq {hardlink} || $clone_type eq {shared}} {
		set objdir [file join $origin_url .git objects]
		if {![file isdirectory $objdir]} {
			set objdir [file join $origin_url objects]
			if {![file isdirectory $objdir]} {
				error_popup [mc "Not a Git repository: %s" [file tail $origin_url]]
				return
			}
		}
	}

	set giturl $origin_url
	if {[is_Cygwin] && [file isdirectory $giturl]} {
		set giturl [exec cygpath --unix --absolute $giturl]
		if {$clone_type eq {shared}} {
			set objdir [exec cygpath --unix --absolute $objdir]
		}
	}

	if {![_git_init $this]} return
	set local_path [pwd]

	if {[catch {
			git config remote.$origin_name.url $giturl
			git config remote.$origin_name.fetch +refs/heads/*:refs/remotes/$origin_name/*
		} err]} {
		error_popup [strcat [mc "Failed to configure origin"] "\n\n$err"]
		return
	}

	destroy $w_body $w_next

	switch -exact -- $clone_type {
	hardlink {
		set o_cons [status_bar::two_line $w_body]
		pack $w_body -fill x -padx 10 -pady 10

		$o_cons start \
			[mc "Counting objects"] \
			[mc "buckets"]
		update

		if {[file exists [file join $objdir info alternates]]} {
			set pwd [pwd]
			if {[catch {
				file mkdir [gitdir objects info]
				set f_in [open [file join $objdir info alternates] r]
				set f_cp [open [gitdir objects info alternates] w]
				fconfigure $f_in -translation binary -encoding binary
				fconfigure $f_cp -translation binary -encoding binary
				cd $objdir
				while {[gets $f_in line] >= 0} {
					if {[is_Cygwin]} {
						puts $f_cp [exec cygpath --unix --absolute $line]
					} else {
						puts $f_cp [file normalize $line]
					}
				}
				close $f_in
				close $f_cp
				cd $pwd
			} err]} {
				catch {cd $pwd}
				_clone_failed $this [mc "Unable to copy objects/info/alternates: %s" $err]
				return
			}
		}

		set tolink  [list]
		set buckets [glob \
			-tails \
			-nocomplain \
			-directory [file join $objdir] ??]
		set bcnt [expr {[llength $buckets] + 2}]
		set bcur 1
		$o_cons update $bcur $bcnt
		update

		file mkdir [file join .git objects pack]
		foreach i [glob -tails -nocomplain \
			-directory [file join $objdir pack] *] {
			lappend tolink [file join pack $i]
		}
		$o_cons update [incr bcur] $bcnt
		update

		foreach i $buckets {
			file mkdir [file join .git objects $i]
			foreach j [glob -tails -nocomplain \
				-directory [file join $objdir $i] *] {
				lappend tolink [file join $i $j]
			}
			$o_cons update [incr bcur] $bcnt
			update
		}
		$o_cons stop

		if {$tolink eq {}} {
			info_popup [strcat \
				[mc "Nothing to clone from %s." $origin_url] \
				"\n" \
				[mc "The 'master' branch has not been initialized."] \
				]
			destroy $w_body
			set done 1
			return
		}

		set i [lindex $tolink 0]
		if {[catch {
				file link -hard \
					[file join .git objects $i] \
					[file join $objdir $i]
			} err]} {
			info_popup [mc "Hardlinks are unavailable.  Falling back to copying."]
			set i [_copy_files $this $objdir $tolink]
		} else {
			set i [_link_files $this $objdir [lrange $tolink 1 end]]
		}
		if {!$i} return

		destroy $w_body
	}
	full {
		set o_cons [console::embed \
			$w_body \
			[mc "Cloning from %s" $origin_url]]
		pack $w_body -fill both -expand 1 -padx 10
		$o_cons exec \
			[list git fetch --no-tags -k $origin_name] \
			[cb _do_clone_tags]
	}
	shared {
		set fd [open [gitdir objects info alternates] w]
		fconfigure $fd -translation binary
		puts $fd $objdir
		close $fd
	}
	}

	if {$clone_type eq {hardlink} || $clone_type eq {shared}} {
		if {![_clone_refs $this]} return
		set pwd [pwd]
		if {[catch {
				cd $origin_url
				set HEAD [git rev-parse --verify HEAD^0]
			} err]} {
			_clone_failed $this [mc "Not a Git repository: %s" [file tail $origin_url]]
			return 0
		}
		cd $pwd
		_do_clone_checkout $this $HEAD
	}
}

method _copy_files {objdir tocopy} {
	$o_cons start \
		[mc "Copying objects"] \
		[mc "KiB"]
	set tot 0
	set cmp 0
	foreach p $tocopy {
		incr tot [file size [file join $objdir $p]]
	}
	foreach p $tocopy {
		if {[catch {
				set f_in [open [file join $objdir $p] r]
				set f_cp [open [file join .git objects $p] w]
				fconfigure $f_in -translation binary -encoding binary
				fconfigure $f_cp -translation binary -encoding binary

				while {![eof $f_in]} {
					incr cmp [fcopy $f_in $f_cp -size 16384]
					$o_cons update \
						[expr {$cmp / 1024}] \
						[expr {$tot / 1024}]
					update
				}

				close $f_in
				close $f_cp
			} err]} {
			_clone_failed $this [mc "Unable to copy object: %s" $err]
			return 0
		}
	}
	return 1
}

method _link_files {objdir tolink} {
	set total [llength $tolink]
	$o_cons start \
		[mc "Linking objects"] \
		[mc "objects"]
	for {set i 0} {$i < $total} {} {
		set p [lindex $tolink $i]
		if {[catch {
				file link -hard \
					[file join .git objects $p] \
					[file join $objdir $p]
			} err]} {
			_clone_failed $this [mc "Unable to hardlink object: %s" $err]
			return 0
		}

		incr i
		if {$i % 5 == 0} {
			$o_cons update $i $total
			update
		}
	}
	return 1
}

method _clone_refs {} {
	set pwd [pwd]
	if {[catch {cd $origin_url} err]} {
		error_popup [mc "Not a Git repository: %s" [file tail $origin_url]]
		return 0
	}
	set fd_in [git_read for-each-ref \
		--tcl \
		{--format=list %(refname) %(objectname) %(*objectname)}]
	cd $pwd

	set fd [open [gitdir packed-refs] w]
	fconfigure $fd -translation binary
	puts $fd "# pack-refs with: peeled"
	while {[gets $fd_in line] >= 0} {
		set line [eval $line]
		set refn [lindex $line 0]
		set robj [lindex $line 1]
		set tobj [lindex $line 2]

		if {[regsub ^refs/heads/ $refn \
			"refs/remotes/$origin_name/" refn]} {
			puts $fd "$robj $refn"
		} elseif {[string match refs/tags/* $refn]} {
			puts $fd "$robj $refn"
			if {$tobj ne {}} {
				puts $fd "^$tobj"
			}
		}
	}
	close $fd_in
	close $fd
	return 1
}

method _do_clone_tags {ok} {
	if {$ok} {
		$o_cons exec \
			[list git fetch --tags -k $origin_name] \
			[cb _do_clone_HEAD]
	} else {
		$o_cons done $ok
		_clone_failed $this [mc "Cannot fetch branches and objects.  See console output for details."]
	}
}

method _do_clone_HEAD {ok} {
	if {$ok} {
		$o_cons exec \
			[list git fetch $origin_name HEAD] \
			[cb _do_clone_full_end]
	} else {
		$o_cons done $ok
		_clone_failed $this [mc "Cannot fetch tags.  See console output for details."]
	}
}

method _do_clone_full_end {ok} {
	$o_cons done $ok

	if {$ok} {
		destroy $w_body

		set HEAD {}
		if {[file exists [gitdir FETCH_HEAD]]} {
			set fd [open [gitdir FETCH_HEAD] r]
			while {[gets $fd line] >= 0} {
				if {[regexp "^(.{40})\t\t" $line line HEAD]} {
					break
				}
			}
			close $fd
		}

		catch {git pack-refs}
		_do_clone_checkout $this $HEAD
	} else {
		_clone_failed $this [mc "Cannot determine HEAD.  See console output for details."]
	}
}

method _clone_failed {{why {}}} {
	if {[catch {file delete -force $local_path} err]} {
		set why [strcat \
			$why \
			"\n\n" \
			[mc "Unable to cleanup %s" $local_path] \
			"\n\n" \
			$err]
	}
	if {$why ne {}} {
		update
		error_popup [strcat [mc "Clone failed."] "\n" $why]
	}
}

method _do_clone_checkout {HEAD} {
	if {$HEAD eq {}} {
		info_popup [strcat \
			[mc "No default branch obtained."] \
			"\n" \
			[mc "The 'master' branch has not been initialized."] \
			]
		set done 1
		return
	}
	if {[catch {
			git update-ref HEAD $HEAD^0
		} err]} {
		info_popup [strcat \
			[mc "Cannot resolve %s as a commit." $HEAD^0] \
			"\n  $err" \
			"\n" \
			[mc "The 'master' branch has not been initialized."] \
			]
		set done 1
		return
	}

	set o_cons [status_bar::two_line $w_body]
	pack $w_body -fill x -padx 10 -pady 10
	$o_cons start \
		[mc "Creating working directory"] \
		[mc "files"]

	set readtree_err {}
	set fd [git_read --stderr read-tree \
		-m \
		-u \
		-v \
		HEAD \
		HEAD \
		]
	fconfigure $fd -blocking 0 -translation binary
	fileevent $fd readable [cb _readtree_wait $fd]
}

method _readtree_wait {fd} {
	set buf [read $fd]
	$o_cons update_meter $buf
	append readtree_err $buf

	fconfigure $fd -blocking 1
	if {![eof $fd]} {
		fconfigure $fd -blocking 0
		return
	}

	if {[catch {close $fd}]} {
		set err $readtree_err
		regsub {^fatal: } $err {} err
		error_popup [strcat \
			[mc "Initial file checkout failed."] \
			"\n\n$err"]
		return
	}

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

	frame $w_body
	label $w_body.h \
		-font font_uibold \
		-text [mc "Open Existing Repository"]
	pack $w_body.h -side top -fill x -pady 10
	pack $w_body -fill x -padx 10

	frame $w_body.where
	label $w_body.where.l -text [mc "Repository:"]
	entry $w_body.where.t \
		-textvariable @local_path \
		-font font_diff \
		-width 50
	button $w_body.where.b \
		-text [mc "Browse"] \
		-command [cb _open_local_path]

	pack $w_body.where.b -side right
	pack $w_body.where.l -side left
	pack $w_body.where.t -fill x
	pack $w_body.where -fill x

	trace add variable @local_path write [cb _write_local_path]
	update
	focus $w_body.where.t
}

method _open_local_path {} {
	if {$local_path ne {}} {
		set p $local_path
	} else {
		set p [_home]
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
	if {![_is_git [file join $local_path .git]]} {
		error_popup [mc "Not a Git repository: %s" [file tail $local_path]]
		return
	}

	if {[catch {cd $local_path} err]} {
		error_popup [strcat \
			[mc "Failed to open repository %s:" $local_path] \
			"\n\n$err"]
		return
	}

	set ::_gitdir .git
	set ::_prefix {}
	set done 1
}

}
