# goto line number
# based on code from gitk, Copyright (C) Paul Mackerras

class linebar {

field w
field ctext

field linenum   {}

constructor new {i_w i_text args} {
	global use_ttk NS
	set w      $i_w
	set ctext  $i_text

	${NS}::frame  $w
	${NS}::label  $w.l       -text [mc "Goto Line:"]
	tentry  $w.ent \
		-textvariable ${__this}::linenum \
		-background lightgreen \
		-validate key \
		-validatecommand [cb _validate %P]
	${NS}::button $w.bn      -text [mc Go] -command [cb _goto]

	pack   $w.l   -side left
	pack   $w.bn  -side right
	pack   $w.ent -side left -expand 1 -fill x

	eval grid conf $w -sticky we $args
	grid remove $w

	trace add variable linenum write [cb _goto_cb]
	bind $w.ent <Return> [cb _goto]
	bind $w.ent <Escape> [cb hide]

	bind $w <Destroy> [list delete_this $this]
	return $this
}

method show {} {
	if {![visible $this]} {
		grid $w
	}
	focus -force $w.ent
}

method hide {} {
	if {[visible $this]} {
		$w.ent delete 0 end
		focus $ctext
		grid remove $w
	}
}

method visible {} {
	return [winfo ismapped $w]
}

method editor {} {
	return $w.ent
}

method _validate {P} {
	# only accept numbers as input
	string is integer $P
}

method _goto_cb {name ix op} {
	after idle [cb _goto 1]
}

method _goto {{nohide {0}}} {
	if {$linenum ne {}} {
		$ctext see $linenum.0
		if {!$nohide} {
			hide $this
		}
	}
}

}
