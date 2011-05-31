# git-gui options editor
# Copyright (C) 2006, 2007 Shawn Pearce

proc config_check_encodings {} {
	global repo_config_new global_config_new

	set enc $global_config_new(gui.encoding)
	if {$enc eq {}} {
		set global_config_new(gui.encoding) [encoding system]
	} elseif {[tcl_encoding $enc] eq {}} {
		error_popup [mc "Invalid global encoding '%s'" $enc]
		return 0
	}

	set enc $repo_config_new(gui.encoding)
	if {$enc eq {}} {
		set repo_config_new(gui.encoding) [encoding system]
	} elseif {[tcl_encoding $enc] eq {}} {
		error_popup [mc "Invalid repo encoding '%s'" $enc]
		return 0
	}

	return 1
}

proc save_config {} {
	global default_config font_descs
	global repo_config global_config system_config
	global repo_config_new global_config_new
	global ui_comm_spell

	foreach option $font_descs {
		set name [lindex $option 0]
		set font [lindex $option 1]
		font configure $font \
			-family $global_config_new(gui.$font^^family) \
			-size $global_config_new(gui.$font^^size)
		font configure ${font}bold \
			-family $global_config_new(gui.$font^^family) \
			-size $global_config_new(gui.$font^^size)
		font configure ${font}italic \
			-family $global_config_new(gui.$font^^family) \
			-size $global_config_new(gui.$font^^size)
		set global_config_new(gui.$name) [font configure $font]
		unset global_config_new(gui.$font^^family)
		unset global_config_new(gui.$font^^size)
	}

	foreach name [array names default_config] {
		set value $global_config_new($name)
		if {$value ne $global_config($name)} {
			if {$value eq $system_config($name)} {
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

	if {[info exists repo_config(gui.spellingdictionary)]} {
		set value $repo_config(gui.spellingdictionary)
		if {$value eq {none}} {
			if {[info exists ui_comm_spell]} {
				$ui_comm_spell stop
			}
		} elseif {[info exists ui_comm_spell]} {
			$ui_comm_spell lang $value
		}
	}
}

proc do_options {} {
	global repo_config global_config font_descs
	global repo_config_new global_config_new
	global ui_comm_spell use_ttk NS

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
	Dialog $w
	wm withdraw $w
	wm transient $w [winfo parent $w]
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	${NS}::frame $w.buttons
	${NS}::button $w.buttons.restore -text [mc "Restore Defaults"] \
		-default normal \
		-command do_restore_defaults
	pack $w.buttons.restore -side left
	${NS}::button $w.buttons.save -text [mc Save] \
		-default active \
		-command [list do_save_config $w]
	pack $w.buttons.save -side right
	${NS}::button $w.buttons.cancel -text [mc "Cancel"] \
		-default normal \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	${NS}::labelframe $w.repo -text [mc "%s Repository" [reponame]]
	${NS}::labelframe $w.global -text [mc "Global (All Repositories)"]
	pack $w.repo -side left -fill both -expand 1 -pady 5 -padx 5
	pack $w.global -side right -fill both -expand 1 -pady 5 -padx 5

	set optid 0
	foreach option {
		{t user.name {mc "User Name"}}
		{t user.email {mc "Email Address"}}

		{b merge.summary {mc "Summarize Merge Commits"}}
		{i-1..5 merge.verbosity {mc "Merge Verbosity"}}
		{b merge.diffstat {mc "Show Diffstat After Merge"}}
		{t merge.tool {mc "Use Merge Tool"}}

		{b gui.trustmtime  {mc "Trust File Modification Timestamps"}}
		{b gui.pruneduringfetch {mc "Prune Tracking Branches During Fetch"}}
		{b gui.matchtrackingbranch {mc "Match Tracking Branches"}}
		{b gui.textconv {mc "Use Textconv For Diffs and Blames"}}
		{b gui.fastcopyblame {mc "Blame Copy Only On Changed Files"}}
		{i-20..200 gui.copyblamethreshold {mc "Minimum Letters To Blame Copy On"}}
		{i-0..300 gui.blamehistoryctx {mc "Blame History Context Radius (days)"}}
		{i-1..99 gui.diffcontext {mc "Number of Diff Context Lines"}}
		{i-0..99 gui.commitmsgwidth {mc "Commit Message Text Width"}}
		{t gui.newbranchtemplate {mc "New Branch Name Template"}}
		{c gui.encoding {mc "Default File Contents Encoding"}}
		} {
		set type [lindex $option 0]
		set name [lindex $option 1]
		set text [eval [lindex $option 2]]
		incr optid
		foreach f {repo global} {
			switch -glob -- $type {
			b {
				${NS}::checkbutton $w.$f.$optid -text $text \
					-variable ${f}_config_new($name) \
					-onvalue true \
					-offvalue false
				pack $w.$f.$optid -side top -anchor w
			}
			i-* {
				regexp -- {-(\d+)\.\.(\d+)$} $type _junk min max
				${NS}::frame $w.$f.$optid
				${NS}::label $w.$f.$optid.l -text "$text:"
				pack $w.$f.$optid.l -side left -anchor w -fill x
				tspinbox $w.$f.$optid.v \
					-textvariable ${f}_config_new($name) \
					-from $min \
					-to $max \
					-increment 1 \
					-width [expr {1 + [string length $max]}]
				bind $w.$f.$optid.v <FocusIn> {%W selection range 0 end}
				pack $w.$f.$optid.v -side right -anchor e -padx 5
				pack $w.$f.$optid -side top -anchor w -fill x
			}
			c -
			t {
				${NS}::frame $w.$f.$optid
				${NS}::label $w.$f.$optid.l -text "$text:"
				${NS}::entry $w.$f.$optid.v \
					-width 20 \
					-textvariable ${f}_config_new($name)
				pack $w.$f.$optid.l -side left -anchor w
				pack $w.$f.$optid.v -side left -anchor w \
					-fill x -expand 1 \
					-padx 5
				if {$type eq {c}} {
					menu $w.$f.$optid.m
					build_encoding_menu $w.$f.$optid.m \
						[list set ${f}_config_new($name)] 1
					${NS}::button $w.$f.$optid.b \
						-text [mc "Change"] \
						-command [list popup_btn_menu \
							$w.$f.$optid.m $w.$f.$optid.b]
					pack $w.$f.$optid.b -side left -anchor w
				}
				pack $w.$f.$optid -side top -anchor w -fill x
			}
			}
		}
	}

	set all_dicts [linsert \
		[spellcheck::available_langs] \
		0 \
		none]
	incr optid
	foreach f {repo global} {
		if {![info exists ${f}_config_new(gui.spellingdictionary)]} {
			if {[info exists ui_comm_spell]} {
				set value [$ui_comm_spell lang]
			} else {
				set value none
			}
			set ${f}_config_new(gui.spellingdictionary) $value
		}

		${NS}::frame $w.$f.$optid
		${NS}::label $w.$f.$optid.l -text [mc "Spelling Dictionary:"]
		if {$use_ttk} {
			ttk::combobox $w.$f.$optid.v \
				-textvariable ${f}_config_new(gui.spellingdictionary) \
				-values $all_dicts -state readonly
		} else {
			eval tk_optionMenu $w.$f.$optid.v \
				${f}_config_new(gui.spellingdictionary) \
				$all_dicts
		}
		pack $w.$f.$optid.l -side left -anchor w -fill x
		pack $w.$f.$optid.v -side right -anchor e -padx 5
		pack $w.$f.$optid -side top -anchor w -fill x
	}
	unset all_dicts

	set all_fonts [lsort [font families]]
	foreach option $font_descs {
		set name [lindex $option 0]
		set font [lindex $option 1]
		set text [eval [lindex $option 2]]

		set global_config_new(gui.$font^^family) \
			[font configure $font -family]
		set global_config_new(gui.$font^^size) \
			[font configure $font -size]

		${NS}::frame $w.global.$name
		${NS}::label $w.global.$name.l -text "$text:"
		${NS}::button $w.global.$name.b \
			-text [mc "Change Font"] \
			-command [list \
				tchoosefont \
				$w \
				[mc "Choose %s" $text] \
				global_config_new(gui.$font^^family) \
				global_config_new(gui.$font^^size) \
				]
		${NS}::label $w.global.$name.f -textvariable global_config_new(gui.$font^^family)
		${NS}::label $w.global.$name.s -textvariable global_config_new(gui.$font^^size)
		${NS}::label $w.global.$name.pt -text [mc "pt."]
		pack $w.global.$name.l -side left -anchor w
		pack $w.global.$name.b -side right -anchor e
		pack $w.global.$name.pt -side right -anchor w
		pack $w.global.$name.s -side right -anchor w
		pack $w.global.$name.f -side right -anchor w
		pack $w.global.$name -side top -anchor w -fill x
	}

	bind $w <Visibility> "grab $w; focus $w.buttons.save"
	bind $w <Key-Escape> "destroy $w"
	bind $w <Key-Return> [list do_save_config $w]

	if {[is_MacOSX]} {
		set t [mc "Preferences"]
	} else {
		set t [mc "Options"]
	}
	wm title $w "[appname] ([reponame]): $t"
	wm deiconify $w
	tkwait window $w
}

proc do_restore_defaults {} {
	global font_descs default_config repo_config system_config
	global repo_config_new global_config_new

	foreach name [array names default_config] {
		set repo_config_new($name) $system_config($name)
		set global_config_new($name) $system_config($name)
	}

	foreach option $font_descs {
		set name [lindex $option 0]
		set repo_config(gui.$name) $system_config(gui.$name)
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
	if {![config_check_encodings]} return
	if {[catch {save_config} err]} {
		error_popup [strcat [mc "Failed to completely save options:"] "\n\n$err"]
	}
	reshow_diff
	destroy $w
}
