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
	entry  $w.ent -textvariable ${__this}::linenum -background lightgreen
	${NS}::button $w.bn      -text [mc Go] -command [cb _incrgoto]

	pack   $w.l   -side left
	pack   $w.bn  -side right
	pack   $w.ent -side left -expand 1 -fill x

	eval grid conf $w -sticky we $args
	grid remove $w

	bind $w.ent <Return> [cb _incrgoto]
	bind $w.ent <Escape> [list linebar::hide $this]

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

method _incrgoto {} {
	if {$linenum ne {}} {
		$ctext see $linenum.0
		hide $this
	}
}

}
