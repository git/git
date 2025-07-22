# git-gui Tools menu dialogs

class tools_add {

field w              ; # widget path
field w_name         ; # new remote name widget
field w_cmd          ; # new remote location widget

field name         {}; # name of the tool
field command      {}; # command to execute
field add_global    0; # add to the --global config
field no_console    0; # disable using the console
field needs_file    0; # ensure filename is set
field confirm       0; # ask for confirmation
field ask_branch    0; # ask for a revision
field ask_args      0; # ask for additional args

constructor dialog {} {
	global repo_config

	make_dialog top w
	wm title $top [mc "%s (%s): Add Tool" [appname] [reponame]]
	if {$top ne {.}} {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
		wm transient $top .
	}

	ttk::label $w.header -text [mc "Add New Tool Command"] \
		-font font_uibold -anchor center
	pack $w.header -side top -fill x

	ttk::frame $w.buttons
	ttk::checkbutton $w.buttons.global \
		-text [mc "Add globally"] \
		-variable @add_global
	pack $w.buttons.global -side left -padx 5
	ttk::button $w.buttons.create -text [mc Add] \
		-default active \
		-command [cb _add]
	pack $w.buttons.create -side right
	ttk::button $w.buttons.cancel -text [mc Cancel] \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	ttk::labelframe $w.desc -text [mc "Tool Details"]

	ttk::label $w.desc.name_cmnt -anchor w\
		-text [mc "Use '/' separators to create a submenu tree:"]
	grid x $w.desc.name_cmnt -sticky we -padx {0 5} -pady {0 2}
	ttk::label $w.desc.name_l -text [mc "Name:"]
	set w_name $w.desc.name_t
	ttk::entry $w_name \
		-width 40 \
		-textvariable @name \
		-validate key \
		-validatecommand [cb _validate_name %d %S]
	grid $w.desc.name_l $w_name -sticky we -padx {0 5}

	ttk::label $w.desc.cmd_l -text [mc "Command:"]
	set w_cmd $w.desc.cmd_t
	ttk::entry $w_cmd \
		-width 40 \
		-textvariable @command
	grid $w.desc.cmd_l $w_cmd -sticky we -padx {0 5} -pady {0 3}

	grid columnconfigure $w.desc 1 -weight 1
	pack $w.desc -anchor nw -fill x -pady 5 -padx 5

	ttk::checkbutton $w.confirm \
		-text [mc "Show a dialog before running"] \
		-variable @confirm -command [cb _check_enable_dlg]

	ttk::labelframe $w.dlg -labelwidget $w.confirm

	ttk::checkbutton $w.dlg.askbranch \
		-text [mc "Ask the user to select a revision (sets \$REVISION)"] \
		-variable @ask_branch -state disabled
	pack $w.dlg.askbranch -anchor w -padx 15

	ttk::checkbutton $w.dlg.askargs \
		-text [mc "Ask the user for additional arguments (sets \$ARGS)"] \
		-variable @ask_args -state disabled
	pack $w.dlg.askargs -anchor w -padx 15

	pack $w.dlg -anchor nw -fill x -pady {0 8} -padx 5

	ttk::checkbutton $w.noconsole \
		-text [mc "Don't show the command output window"] \
		-variable @no_console
	pack $w.noconsole -anchor w -padx 5

	ttk::checkbutton $w.needsfile \
		-text [mc "Run only if a diff is selected (\$FILENAME not empty)"] \
		-variable @needs_file
	pack $w.needsfile -anchor w -padx 5

	bind $w <Visibility> [cb _visible]
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Key-Return> [cb _add]\;break
	tkwait window $w
}

method _check_enable_dlg {} {
	if {$confirm} {
		$w.dlg.askbranch configure -state normal
		$w.dlg.askargs configure -state normal
	} else {
		$w.dlg.askbranch configure -state disabled
		$w.dlg.askargs configure -state disabled
	}
}

method _add {} {
	global repo_config

	if {$name eq {}} {
		error_popup [mc "Please supply a name for the tool."]
		focus $w_name
		return
	}

	set item "guitool.$name.cmd"

	if {[info exists repo_config($item)]} {
		error_popup [mc "Tool '%s' already exists." $name]
		focus $w_name
		return
	}

	set cmd [list git config]
	if {$add_global} { lappend cmd --global }
	set items {}
	if {$no_console} { lappend items "guitool.$name.noconsole" }
	if {$needs_file} { lappend items "guitool.$name.needsfile" }
	if {$confirm} {
		if {$ask_args}   { lappend items "guitool.$name.argprompt" }
		if {$ask_branch} { lappend items "guitool.$name.revprompt" }
		if {!$ask_args && !$ask_branch} {
			lappend items "guitool.$name.confirm"
		}
	}

	if {[catch {
		eval $cmd [list $item $command]
		foreach citem $items { eval $cmd [list $citem yes] }
	    } err]} {
		error_popup [mc "Could not add tool:\n%s" $err]
	} else {
		set repo_config($item) $command
		foreach citem $items { set repo_config($citem) yes }

		tools_populate_all
	}

	destroy $w
}

method _validate_name {d S} {
	if {$d == 1} {
		if {[regexp {[~?*&\[\0\"\\\{]} $S]} {
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

class tools_remove {

field w              ; # widget path
field w_names        ; # name list

constructor dialog {} {
	global repo_config global_config system_config

	load_config 1

	make_dialog top w
	wm title $top [mc "%s (%s): Remove Tool" [appname] [reponame]]
	if {$top ne {.}} {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
		wm transient $top .
	}

	ttk::label $w.header -text [mc "Remove Tool Commands"] \
		-font font_uibold -anchor center
	pack $w.header -side top -fill x

	ttk::frame $w.buttons
	ttk::button $w.buttons.create -text [mc Remove] \
		-default active \
		-command [cb _remove]
	pack $w.buttons.create -side right
	ttk::button $w.buttons.cancel -text [mc Cancel] \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	ttk::frame $w.list
	set w_names $w.list.l
	slistbox $w_names \
		-height 10 \
		-width 30 \
		-selectmode extended \
		-exportselection false
	pack $w.list.l -side left -fill both -expand 1
	pack $w.list -fill both -expand 1 -pady 5 -padx 5

	set local_cnt 0
	foreach fullname [tools_list] {
		# Cannot delete system tools
		if {[info exists system_config(guitool.$fullname.cmd)]} continue

		$w_names insert end $fullname
		if {![info exists global_config(guitool.$fullname.cmd)]} {
			$w_names itemconfigure end -foreground blue
			incr local_cnt
		}
	}

	if {$local_cnt > 0} {
		ttk::label $w.colorlbl -foreground blue \
			-text [mc "(Blue denotes repository-local tools)"]
		pack $w.colorlbl -fill x -pady 5 -padx 5
	}

	bind $w <Visibility> [cb _visible]
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Key-Return> [cb _remove]\;break
	tkwait window $w
}

method _remove {} {
	foreach i [$w_names curselection] {
		set name [$w_names get $i]

		catch { git config --remove-section guitool.$name }
		catch { git config --global --remove-section guitool.$name }
	}

	load_config 0
	tools_populate_all

	destroy $w
}

method _visible {} {
	grab $w
	focus $w_names
}

}

class tools_askdlg {

field w              ; # widget path
field w_rev        {}; # revision browser
field w_args       {}; # arguments

field is_ask_args   0; # has arguments field
field is_ask_revs   0; # has revision browser

field is_ok         0; # ok to start
field argstr       {}; # arguments

constructor dialog {fullname} {
	global M1B

	set title [get_config "guitool.$fullname.title"]
	if {$title eq {}} {
		regsub {/} $fullname { / } title
	}

	make_dialog top w -autodelete 0
	wm title $top "[mc "%s (%s):" [appname] [reponame]] $title"
	if {$top ne {.}} {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
		wm transient $top .
	}

	set prompt [get_config "guitool.$fullname.prompt"]
	if {$prompt eq {}} {
		set command [get_config "guitool.$fullname.cmd"]
		set prompt [mc "Run Command: %s" $command]
	}

	ttk::label $w.header -text $prompt -font font_uibold -anchor center
	pack $w.header -side top -fill x

	set argprompt [get_config "guitool.$fullname.argprompt"]
	set revprompt [get_config "guitool.$fullname.revprompt"]

	set is_ask_args [expr {$argprompt ne {}}]
	set is_ask_revs [expr {$revprompt ne {}}]

	if {$is_ask_args} {
		if {$argprompt eq {yes} || $argprompt eq {true} || $argprompt eq {1}} {
			set argprompt [mc "Arguments"]
		}

		ttk::labelframe $w.arg -text $argprompt

		set w_args $w.arg.txt
		ttk::entry $w_args \
			-width 40 \
			-textvariable @argstr
		pack $w_args -padx 5 -pady 5 -fill both
		pack $w.arg -anchor nw -fill both -pady 5 -padx 5
	}

	if {$is_ask_revs} {
		if {$revprompt eq {yes} || $revprompt eq {true} || $revprompt eq {1}} {
			set revprompt [mc "Revision"]
		}

		if {[is_config_true "guitool.$fullname.revunmerged"]} {
			set w_rev [::choose_rev::new_unmerged $w.rev $revprompt]
		} else {
			set w_rev [::choose_rev::new $w.rev $revprompt]
		}

		pack $w.rev -anchor nw -fill both -expand 1 -pady 5 -padx 5
	}

	ttk::frame $w.buttons
	if {$is_ask_revs} {
		ttk::button $w.buttons.visualize \
			-text [mc Visualize] \
			-command [cb _visualize]
		pack $w.buttons.visualize -side left
	}
	ttk::button $w.buttons.ok \
		-text [mc OK] \
		-command [cb _start]
	pack $w.buttons.ok -side right
	ttk::button $w.buttons.cancel \
		-text [mc "Cancel"] \
		-command [cb _cancel]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	bind $w <$M1B-Key-Return> [cb _start]
	bind $w <Key-Return> [cb _start]
	bind $w <Key-Escape> [cb _cancel]
	wm protocol $w WM_DELETE_WINDOW [cb _cancel]

	bind $w <Visibility> [cb _visible]
	return $this
}

method execute {} {
	tkwait window $w
	set rv $is_ok
	delete_this
	return $rv
}

method _visible {} {
	grab $w
	if {$is_ask_args} {
		focus $w_args
	} elseif {$is_ask_revs} {
		$w_rev focus_filter
	}
}

method _cancel {} {
	wm protocol $w WM_DELETE_WINDOW {}
	destroy $w
}

method _rev {} {
	if {[catch {$w_rev commit_or_die}]} {
		return {}
	}
	return [$w_rev get]
}

method _visualize {} {
	global current_branch
	set rev [_rev $this]
	if {$rev ne {}} {
		do_gitk [list --left-right "$current_branch...$rev"]
	}
}

method _start {} {
	global env

	if {$is_ask_revs} {
		set name [_rev $this]
		if {$name eq {}} {
			return
		}
		set env(REVISION) $name
	}

	if {$is_ask_args} {
		set env(ARGS) $argstr
	}

	set is_ok 1
	_cancel $this
}

}
