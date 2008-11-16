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

constructor dialog {} {
	global repo_config

	make_toplevel top w
	wm title $top [append "[appname] ([reponame]): " [mc "Add Tool"]]
	if {$top ne {.}} {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
		wm transient $top .
	}

	label $w.header -text [mc "Add New Tool Command"] -font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	checkbutton $w.buttons.global \
		-text [mc "Add globally"] \
		-variable @add_global
	pack $w.buttons.global -side left -padx 5
	button $w.buttons.create -text [mc Add] \
		-default active \
		-command [cb _add]
	pack $w.buttons.create -side right
	button $w.buttons.cancel -text [mc Cancel] \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	labelframe $w.desc -text [mc "Tool Details"]

	label $w.desc.name_cmnt -anchor w\
		-text [mc "Use '/' separators to create a submenu tree:"]
	grid x $w.desc.name_cmnt -sticky we -padx {0 5} -pady {0 2}
	label $w.desc.name_l -text [mc "Name:"]
	set w_name $w.desc.name_t
	entry $w_name \
		-borderwidth 1 \
		-relief sunken \
		-width 40 \
		-textvariable @name \
		-validate key \
		-validatecommand [cb _validate_name %d %S]
	grid $w.desc.name_l $w_name -sticky we -padx {0 5}

	label $w.desc.cmd_l -text [mc "Command:"]
	set w_cmd $w.desc.cmd_t
	entry $w_cmd \
		-borderwidth 1 \
		-relief sunken \
		-width 40 \
		-textvariable @command
	grid $w.desc.cmd_l $w_cmd -sticky we -padx {0 5} -pady {0 3}

	grid columnconfigure $w.desc 1 -weight 1
	pack $w.desc -anchor nw -fill x -pady 5 -padx 5

	checkbutton $w.confirm \
		-text [mc "Ask for confirmation before running"] \
		-variable @confirm
	pack $w.confirm -anchor w -pady {5 0} -padx 5

	checkbutton $w.noconsole \
		-text [mc "Don't show the command output window"] \
		-variable @no_console
	pack $w.noconsole -anchor w -padx 5

	checkbutton $w.needsfile \
		-text [mc "Run only if a diff is selected (\$FILENAME not empty)"] \
		-variable @needs_file
	pack $w.needsfile -anchor w -padx 5

	bind $w <Visibility> [cb _visible]
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Key-Return> [cb _add]\;break
	tkwait window $w
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
	if {$confirm}    { lappend items "guitool.$name.confirm" }
	if {$needs_file} { lappend items "guitool.$name.needsfile" }

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

	make_toplevel top w
	wm title $top [append "[appname] ([reponame]): " [mc "Remove Tool"]]
	if {$top ne {.}} {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
		wm transient $top .
	}

	label $w.header -text [mc "Remove Tool Commands"] -font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.create -text [mc Remove] \
		-default active \
		-command [cb _remove]
	pack $w.buttons.create -side right
	button $w.buttons.cancel -text [mc Cancel] \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	frame $w.list
	set w_names $w.list.l
	listbox $w_names \
		-height 10 \
		-width 30 \
		-selectmode extended \
		-exportselection false \
		-yscrollcommand [list $w.list.sby set]
	scrollbar $w.list.sby -command [list $w.list.l yview]
	pack $w.list.sby -side right -fill y
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
		label $w.colorlbl -foreground blue \
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
