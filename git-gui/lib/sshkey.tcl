# git-gui about git-gui dialog
# Copyright (C) 2006, 2007 Shawn Pearce

proc find_ssh_key {} {
	foreach name {
		~/.ssh/id_dsa.pub ~/.ssh/id_ecdsa.pub ~/.ssh/id_ed25519.pub
		~/.ssh/id_rsa.pub ~/.ssh/identity.pub
	} {
		if {[file exists $name]} {
			set fh    [open $name r]
			set cont  [read $fh]
			close $fh
			return [list $name $cont]
		}
	}

	return {}
}

proc do_ssh_key {} {
	global sshkey_title have_tk85 sshkey_fd use_ttk NS

	set w .sshkey_dialog
	if {[winfo exists $w]} {
		raise $w
		return
	}

	Dialog $w
	wm transient $w .

	set finfo [find_ssh_key]
	if {$finfo eq {}} {
		set sshkey_title [mc "No keys found."]
		set gen_state   normal
	} else {
		set sshkey_title [mc "Found a public key in: %s" [lindex $finfo 0]]
		set gen_state   disabled
	}

	${NS}::frame $w.header
	${NS}::label $w.header.lbl -textvariable sshkey_title -anchor w
	${NS}::button $w.header.gen -text [mc "Generate Key"] \
		-command [list make_ssh_key $w] -state $gen_state
	pack $w.header.lbl -side left -expand 1 -fill x
	pack $w.header.gen -side right
	pack $w.header -fill x -pady 5 -padx 5

	text $w.contents -width 60 -height 10 -wrap char -relief sunken
	pack $w.contents -fill both -expand 1
	if {$have_tk85} {
		set clr darkblue
		if {$use_ttk} { set clr [ttk::style lookup . -selectbackground] }
		$w.contents configure -inactiveselectbackground $clr
	}

	${NS}::frame $w.buttons
	${NS}::button $w.buttons.close -text [mc Close] \
		-default active -command [list destroy $w]
	pack $w.buttons.close -side right
	${NS}::button $w.buttons.copy -text [mc "Copy To Clipboard"] \
		-command [list tk_textCopy $w.contents]
	pack $w.buttons.copy -side left
	pack $w.buttons -side bottom -fill x -pady 5 -padx 5

	if {$finfo ne {}} {
		$w.contents insert end [lindex $finfo 1] sel
	}
	$w.contents configure -state disabled

	bind $w <Visibility> "grab $w; focus $w.buttons.close"
	bind $w <Key-Escape> "destroy $w"
	bind $w <Key-Return> "destroy $w"
	bind $w <Destroy> kill_sshkey
	wm title $w [mc "Your OpenSSH Public Key"]
	tk::PlaceWindow $w widget .
	tkwait window $w
}

proc make_ssh_key {w} {
	global sshkey_title sshkey_output sshkey_fd

	set sshkey_title [mc "Generating..."]
	$w.header.gen configure -state disabled

	set cmdline [list sh -c {echo | ssh-keygen -q -t rsa -f ~/.ssh/id_rsa 2>&1}]

	if {[catch { set sshkey_fd [_open_stdout_stderr $cmdline] } err]} {
		error_popup [mc "Could not start ssh-keygen:\n\n%s" $err]
		return
	}

	set sshkey_output {}
	fconfigure $sshkey_fd -blocking 0
	fileevent $sshkey_fd readable [list read_sshkey_output $sshkey_fd $w]
}

proc kill_sshkey {} {
	global sshkey_fd
	if {![info exists sshkey_fd]} return
	catch { kill_file_process $sshkey_fd }
	catch { close $sshkey_fd }
}

proc read_sshkey_output {fd w} {
	global sshkey_fd sshkey_output sshkey_title

	set sshkey_output "$sshkey_output[read $fd]"
	if {![eof $fd]} return

	fconfigure $fd -blocking 1
	unset sshkey_fd

	$w.contents configure -state normal
	if {[catch {close $fd} err]} {
		set sshkey_title [mc "Generation failed."]
		$w.contents insert end $err
		$w.contents insert end "\n"
		$w.contents insert end $sshkey_output
	} else {
		set finfo [find_ssh_key]
		if {$finfo eq {}} {
			set sshkey_title [mc "Generation succeeded, but no keys found."]
			$w.contents insert end $sshkey_output
		} else {
			set sshkey_title [mc "Your key is in: %s" [lindex $finfo 0]]
			$w.contents insert end [lindex $finfo 1] sel
		}
	}
	$w.contents configure -state disable
}
