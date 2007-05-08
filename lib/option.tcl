# git-gui options editor
# Copyright (C) 2006, 2007 Shawn Pearce

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
		-default active \
		-command [list destroy $w]
	pack $w.buttons.close -side right
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	label $w.desc \
		-text "git-gui - a graphical user interface for Git.
$copyright" \
		-padx 5 -pady 5 \
		-justify left \
		-anchor w \
		-borderwidth 1 \
		-relief solid
	pack $w.desc -side top -fill x -padx 5 -pady 5

	set v {}
	append v "git-gui version $appvers\n"
	append v "[git version]\n"
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
		-relief solid
	pack $w.vers -side top -fill x -padx 5 -pady 5

	menu $w.ctxm -tearoff 0
	$w.ctxm add command \
		-label {Copy} \
		-command "
		clipboard clear
		clipboard append -format STRING -type STRING -- \[$w.vers cget -text\]
	"

	bind $w <Visibility> "grab $w; focus $w.buttons.close"
	bind $w <Key-Escape> "destroy $w"
	bind $w <Key-Return> "destroy $w"
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

	label $w.header -text "Options" \
		-font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.restore -text {Restore Defaults} \
		-default normal \
		-command do_restore_defaults
	pack $w.buttons.restore -side left
	button $w.buttons.save -text Save \
		-default active \
		-command [list do_save_config $w]
	pack $w.buttons.save -side right
	button $w.buttons.cancel -text {Cancel} \
		-default normal \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	labelframe $w.repo -text "[reponame] Repository"
	labelframe $w.global -text {Global (All Repositories)}
	pack $w.repo -side left -fill both -expand 1 -pady 5 -padx 5
	pack $w.global -side right -fill both -expand 1 -pady 5 -padx 5

	set optid 0
	foreach option {
		{t user.name {User Name}}
		{t user.email {Email Address}}

		{b merge.summary {Summarize Merge Commits}}
		{i-1..5 merge.verbosity {Merge Verbosity}}

		{b gui.trustmtime  {Trust File Modification Timestamps}}
		{i-1..99 gui.diffcontext {Number of Diff Context Lines}}
		{t gui.newbranchtemplate {New Branch Name Template}}
		} {
		set type [lindex $option 0]
		set name [lindex $option 1]
		set text [lindex $option 2]
		incr optid
		foreach f {repo global} {
			switch -glob -- $type {
			b {
				checkbutton $w.$f.$optid -text $text \
					-variable ${f}_config_new($name) \
					-onvalue true \
					-offvalue false
				pack $w.$f.$optid -side top -anchor w
			}
			i-* {
				regexp -- {-(\d+)\.\.(\d+)$} $type _junk min max
				frame $w.$f.$optid
				label $w.$f.$optid.l -text "$text:"
				pack $w.$f.$optid.l -side left -anchor w -fill x
				spinbox $w.$f.$optid.v \
					-textvariable ${f}_config_new($name) \
					-from $min \
					-to $max \
					-increment 1 \
					-width [expr {1 + [string length $max]}]
				bind $w.$f.$optid.v <FocusIn> {%W selection range 0 end}
				pack $w.$f.$optid.v -side right -anchor e -padx 5
				pack $w.$f.$optid -side top -anchor w -fill x
			}
			t {
				frame $w.$f.$optid
				label $w.$f.$optid.l -text "$text:"
				entry $w.$f.$optid.v \
					-borderwidth 1 \
					-relief sunken \
					-width 20 \
					-textvariable ${f}_config_new($name)
				pack $w.$f.$optid.l -side left -anchor w
				pack $w.$f.$optid.v -side left -anchor w \
					-fill x -expand 1 \
					-padx 5
				pack $w.$f.$optid -side top -anchor w -fill x
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
		label $w.global.$name.l -text "$text:"
		pack $w.global.$name.l -side left -anchor w -fill x
		eval tk_optionMenu $w.global.$name.family \
			global_config_new(gui.$font^^family) \
			$all_fonts
		spinbox $w.global.$name.size \
			-textvariable global_config_new(gui.$font^^size) \
			-from 2 -to 80 -increment 1 \
			-width 3
		bind $w.global.$name.size <FocusIn> {%W selection range 0 end}
		pack $w.global.$name.size -side right -anchor e
		pack $w.global.$name.family -side right -anchor e
		pack $w.global.$name -side top -anchor w -fill x
	}

	bind $w <Visibility> "grab $w; focus $w.buttons.save"
	bind $w <Key-Escape> "destroy $w"
	bind $w <Key-Return> [list do_save_config $w]
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
