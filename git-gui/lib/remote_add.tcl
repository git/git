# git-gui remote adding support
# Copyright (C) 2008 Petr Baudis

class remote_add {

field w              ; # widget path
field w_name         ; # new remote name widget
field w_loc          ; # new remote location widget

field name         {}; # name of the remote the user has chosen
field location     {}; # location of the remote the user has chosen

field opt_action fetch; # action to do after registering the remote locally

constructor dialog {} {
	global repo_config use_ttk NS

	make_dialog top w
	wm withdraw $top
	wm title $top [mc "%s (%s): Add Remote" [appname] [reponame]]
	if {$top ne {.}} {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
	}

	${NS}::label $w.header -text [mc "Add New Remote"] \
		-font font_uibold -anchor center
	pack $w.header -side top -fill x

	${NS}::frame $w.buttons
	${NS}::button $w.buttons.create -text [mc Add] \
		-default active \
		-command [cb _add]
	pack $w.buttons.create -side right
	${NS}::button $w.buttons.cancel -text [mc Cancel] \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	${NS}::labelframe $w.desc -text [mc "Remote Details"]

	${NS}::label $w.desc.name_l -text [mc "Name:"]
	set w_name $w.desc.name_t
	${NS}::entry $w_name \
		-width 40 \
		-textvariable @name \
		-validate key \
		-validatecommand [cb _validate_name %d %S]
	grid $w.desc.name_l $w_name -sticky we -padx {0 5}

	${NS}::label $w.desc.loc_l -text [mc "Location:"]
	set w_loc $w.desc.loc_t
	${NS}::entry $w_loc \
		-width 40 \
		-textvariable @location
	grid $w.desc.loc_l $w_loc -sticky we -padx {0 5}

	grid columnconfigure $w.desc 1 -weight 1
	pack $w.desc -anchor nw -fill x -pady 5 -padx 5

	${NS}::labelframe $w.action -text [mc "Further Action"]

	${NS}::radiobutton $w.action.fetch \
		-text [mc "Fetch Immediately"] \
		-value fetch \
		-variable @opt_action
	pack $w.action.fetch -anchor nw

	${NS}::radiobutton $w.action.push \
		-text [mc "Initialize Remote Repository and Push"] \
		-value push \
		-variable @opt_action
	pack $w.action.push -anchor nw

	${NS}::radiobutton $w.action.none \
		-text [mc "Do Nothing Else Now"] \
		-value none \
		-variable @opt_action
	pack $w.action.none -anchor nw

	grid columnconfigure $w.action 1 -weight 1
	pack $w.action -anchor nw -fill x -pady 5 -padx 5

	bind $w <Visibility> [cb _visible]
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Key-Return> [cb _add]\;break
	wm deiconify $top
	tkwait window $w
}

method _add {} {
	global repo_config env
	global M1B

	if {$name eq {}} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message [mc "Please supply a remote name."]
		focus $w_name
		return
	}

	# XXX: We abuse check-ref-format here, but
	# that should be ok.
	if {[catch {git check-ref-format "remotes/$name"}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message [mc "'%s' is not an acceptable remote name." $name]
		focus $w_name
		return
	}

	if {[catch {add_single_remote $name $location}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message [mc "Failed to add remote '%s' of location '%s'." $name $location]
		focus $w_name
		return
	}

	switch -- $opt_action {
	fetch {
		set c [console::new \
			[mc "fetch %s" $name] \
			[mc "Fetching the %s" $name]]
		console::exec $c [list git fetch $name]
	}
	push {
		set cmds [list]

		# Parse the location
		if { [regexp {(?:git\+)?ssh://([^/]+)(/.+)} $location xx host path]
		     || [regexp {([^:][^:]+):(.+)} $location xx host path]} {
			set ssh ssh
			if {[info exists env(GIT_SSH)]} {
				set ssh $env(GIT_SSH)
			}
			lappend cmds [list exec $ssh $host mkdir -p $location && git --git-dir=$path init --bare]
		} elseif { ! [regexp {://} $location xx] } {
			lappend cmds [list exec mkdir -p $location]
			lappend cmds [list exec git --git-dir=$location init --bare]
		} else {
			tk_messageBox \
				-icon error \
				-type ok \
				-title [wm title $w] \
				-parent $w \
				-message [mc "Do not know how to initialize repository at location '%s'." $location]
			destroy $w
			return
		}

		set c [console::new \
			[mc "push %s" $name] \
			[mc "Setting up the %s (at %s)" $name $location]]

		lappend cmds [list exec git push -v --all $name]
		console::chain $c $cmds
	}
	none {
	}
	}

	destroy $w
}

method _validate_name {d S} {
	if {$d == 1} {
		if {[regexp {[~^:?*\[\0- ]} $S]} {
			return 0
		}
	}
	return 1
}

method _visible {} {
	grab $w
	$w_name icursor end
	focus $w_name
}

}
