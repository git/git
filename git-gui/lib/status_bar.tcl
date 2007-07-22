# git-gui status bar mega-widget
# Copyright (C) 2007 Shawn Pearce

class status_bar {

field w         ; # our own window path
field w_l       ; # text widget we draw messages into
field w_c       ; # canvas we draw a progress bar into
field status  {}; # single line of text we show
field prefix  {}; # text we format into status
field units   {}; # unit of progress
field meter   {}; # current core git progress meter (if active)

constructor new {path} {
	set w $path
	set w_l $w.l
	set w_c $w.c

	frame $w \
		-borderwidth 1 \
		-relief sunken
	label $w_l \
		-textvariable @status \
		-anchor w \
		-justify left
	pack $w_l -side left

	bind $w <Destroy> [cb _delete %W]
	return $this
}

method start {msg uds} {
	if {[winfo exists $w_c]} {
		$w_c coords bar 0 0 0 20
	} else {
		canvas $w_c \
			-width 100 \
			-height [expr {int([winfo reqheight $w_l] * 0.6)}] \
			-borderwidth 1 \
			-relief groove \
			-highlightt 0
		$w_c create rectangle 0 0 0 20 -tags bar -fill navy
		pack $w_c -side right
	}

	set status $msg
	set prefix $msg
	set units  $uds
	set meter  {}
}

method update {have total} {
	set pdone 0
	if {$total > 0} {
		set pdone [expr {100 * $have / $total}]
	}

	set status [format "%s ... %i of %i %s (%2i%%)" \
		$prefix $have $total $units $pdone]
	$w_c coords bar 0 0 $pdone 20
}

method update_meter {buf} {
	append meter $buf
	set r [string last "\r" $meter]
	if {$r == -1} {
		return
	}

	set prior [string range $meter 0 $r]
	set meter [string range $meter [expr {$r + 1}] end]
	if {[regexp "\\((\\d+)/(\\d+)\\)\\s+done\r\$" $prior _j a b]} {
		update $this $a $b
	}
}

method stop {{msg {}}} {
	destroy $w_c
	if {$msg ne {}} {
		set status $msg
	}
}

method show {msg {test {}}} {
	if {$test eq {} || $status eq $test} {
		set status $msg
	}
}

method _delete {current} {
	if {$current eq $w} {
		delete_this
	}
}

}
