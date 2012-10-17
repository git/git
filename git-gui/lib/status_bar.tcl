# git-gui status bar mega-widget
# Copyright (C) 2007 Shawn Pearce

class status_bar {

field w         ; # our own window path
field w_l       ; # text widget we draw messages into
field w_c       ; # canvas we draw a progress bar into
field c_pack    ; # script to pack the canvas with
field status  {}; # single line of text we show
field prefix  {}; # text we format into status
field units   {}; # unit of progress
field meter   {}; # current core git progress meter (if active)

constructor new {path} {
	global use_ttk NS
	set w $path
	set w_l $w.l
	set w_c $w.c

	${NS}::frame $w
	if {!$use_ttk} {
		$w configure -borderwidth 1 -relief sunken
	}
	${NS}::label $w_l \
		-textvariable @status \
		-anchor w \
		-justify left
	pack $w_l -side left
	set c_pack [cb _oneline_pack]

	bind $w <Destroy> [cb _delete %W]
	return $this
}

method _oneline_pack {} {
	$w_c conf -width 100
	pack $w_c -side right
}

constructor two_line {path} {
	global NS
	set w $path
	set w_l $w.l
	set w_c $w.c

	${NS}::frame $w
	${NS}::label $w_l \
		-textvariable @status \
		-anchor w \
		-justify left
	pack $w_l -anchor w -fill x
	set c_pack [list pack $w_c -fill x]

	bind $w <Destroy> [cb _delete %W]
	return $this
}

method start {msg uds} {
	if {[winfo exists $w_c]} {
		$w_c coords bar 0 0 0 20
	} else {
		canvas $w_c \
			-height [expr {int([winfo reqheight $w_l] * 0.6)}] \
			-borderwidth 1 \
			-relief groove \
			-highlightt 0
		$w_c create rectangle 0 0 0 20 -tags bar -fill navy
		eval $c_pack
	}

	set status $msg
	set prefix $msg
	set units  $uds
	set meter  {}
}

method update {have total} {
	set pdone 0
	set cdone 0
	if {$total > 0} {
		set pdone [expr {100 * $have / $total}]
		set cdone [expr {[winfo width $w_c] * $have / $total}]
	}

	set prec [string length [format %i $total]]
	set status [mc "%s ... %*i of %*i %s (%3i%%)" \
		$prefix \
		$prec $have \
		$prec $total \
		$units $pdone]
	$w_c coords bar 0 0 $cdone 20
}

method update_meter {buf} {
	append meter $buf
	set r [string last "\r" $meter]
	if {$r == -1} {
		return
	}

	set prior [string range $meter 0 $r]
	set meter [string range $meter [expr {$r + 1}] end]
	set p "\\((\\d+)/(\\d+)\\)"
	if {[regexp ":\\s*\\d+% $p\(?:, done.\\s*\n|\\s*\r)\$" $prior _j a b]} {
		update $this $a $b
	} elseif {[regexp "$p\\s+done\r\$" $prior _j a b]} {
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
