# git-gui status bar mega-widget
# Copyright (C) 2007 Shawn Pearce

# The status_bar class manages the entire status bar. It is possible for
# multiple overlapping asynchronous operations to want to display status
# simultaneously. Each one receives a status_bar_operation when it calls the
# start method, and the status bar combines all active operations into the
# line of text it displays. Most of the time, there will be at most one
# ongoing operation.
#
# Note that the entire status bar can be either in single-line or two-line
# mode, depending on the constructor. Multiple active operations are only
# supported for single-line status bars.

class status_bar {

field allow_multiple ; # configured at construction

field w         ; # our own window path
field w_l       ; # text widget we draw messages into
field w_c       ; # canvas we draw a progress bar into
field c_pack    ; # script to pack the canvas with

field baseline_text   ; # text to show if there are no operations
field status_bar_text ; # combined text for all operations

field operations ; # list of current ongoing operations

# The status bar can display a progress bar, updated when consumers call the
# update method on their status_bar_operation. When there are multiple
# operations, the status bar shows the combined status of all operations.
#
# When an overlapping operation completes, the progress bar is going to
# abruptly have one fewer operation in the calculation, causing a discontinuity.
# Therefore, whenever an operation completes, if it is not the last operation,
# this counter is increased, and the progress bar is calculated as though there
# were still another operation at 100%. When the last operation completes, this
# is reset to 0.
field completed_operation_count

constructor new {path} {
	global use_ttk NS
	set w $path
	set w_l $w.l
	set w_c $w.c

	# Standard single-line status bar: Permit overlapping operations
	set allow_multiple 1

	set baseline_text ""
	set operations [list]
	set completed_operation_count 0

	${NS}::frame $w
	if {!$use_ttk} {
		$w configure -borderwidth 1 -relief sunken
	}
	${NS}::label $w_l \
		-textvariable @status_bar_text \
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

	# Two-line status bar: Only one ongoing operation permitted.
	set allow_multiple 0

	set baseline_text ""
	set operations [list]
	set completed_operation_count 0

	${NS}::frame $w
	${NS}::label $w_l \
		-textvariable @status_bar_text \
		-anchor w \
		-justify left
	pack $w_l -anchor w -fill x
	set c_pack [list pack $w_c -fill x]

	bind $w <Destroy> [cb _delete %W]
	return $this
}

method ensure_canvas {} {
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
}

method show {msg} {
	$this ensure_canvas
	set baseline_text $msg
	$this refresh
}

method start {msg {uds {}}} {
	set baseline_text ""

	if {!$allow_multiple && [llength $operations]} {
		return [lindex $operations 0]
	}

	$this ensure_canvas

	set operation [status_bar_operation::new $this $msg $uds]

	lappend operations $operation

	$this refresh

	return $operation
}

method refresh {} {
	set new_text ""

	set total [expr $completed_operation_count * 100]
	set have $total

	foreach operation $operations {
		if {$new_text != ""} {
			append new_text " / "
		}

		append new_text [$operation get_status]

		set total [expr $total + 100]
		set have [expr $have + [$operation get_progress]]
	}

	if {$new_text == ""} {
		set new_text $baseline_text
	}

	set status_bar_text $new_text

	if {[winfo exists $w_c]} {
		set pixel_width 0
		if {$have > 0} {
			set pixel_width [expr {[winfo width $w_c] * $have / $total}]
		}

		$w_c coords bar 0 0 $pixel_width 20
	}
}

method stop {operation stop_msg} {
	set idx [lsearch $operations $operation]

	if {$idx >= 0} {
		set operations [lreplace $operations $idx $idx]
		set completed_operation_count [expr \
			$completed_operation_count + 1]

		if {[llength $operations] == 0} {
			set completed_operation_count 0

			destroy $w_c
			if {$stop_msg ne {}} {
				set baseline_text $stop_msg
			}
		}

		$this refresh
	}
}

method stop_all {{stop_msg {}}} {
	# This makes the operation's call to stop a no-op.
	set operations_copy $operations
	set operations [list]

	foreach operation $operations_copy {
		$operation stop
	}

	if {$stop_msg ne {}} {
		set baseline_text $stop_msg
	}

	$this refresh
}

method _delete {current} {
	if {$current eq $w} {
		delete_this
	}
}

}

# The status_bar_operation class tracks a single consumer's ongoing status bar
# activity, with the context that there are a few situations where multiple
# overlapping asynchronous operations might want to display status information
# simultaneously. Instances of status_bar_operation are created by calling
# start on the status_bar, and when the caller is done with its stauts bar
# operation, it calls stop on the operation.

class status_bar_operation {

field status_bar; # reference back to the status_bar that owns this object

field is_active;

field status   {}; # single line of text we show
field progress {}; # current progress (0 to 100)
field prefix   {}; # text we format into status
field units    {}; # unit of progress
field meter    {}; # current core git progress meter (if active)

constructor new {owner msg uds} {
	set status_bar $owner

	set status $msg
	set progress 0
	set prefix $msg
	set units  $uds
	set meter  {}

	set is_active 1

	return $this
}

method get_is_active {} { return $is_active }
method get_status {} { return $status }
method get_progress {} { return $progress }

method update {have total} {
	if {!$is_active} { return }

	set progress 0

	if {$total > 0} {
		set progress [expr {100 * $have / $total}]
	}

	set prec [string length [format %i $total]]

	set status [mc "%s ... %*i of %*i %s (%3i%%)" \
		$prefix \
		$prec $have \
		$prec $total \
		$units $progress]

	$status_bar refresh
}

method update_meter {buf} {
	if {!$is_active} { return }

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

method stop {{stop_msg {}}} {
	if {$is_active} {
		set is_active 0
		$status_bar stop $this $stop_msg
	}
}

method restart {msg} {
	if {!$is_active} { return }

	set status $msg
	set prefix $msg
	set meter {}
	$status_bar refresh
}

method _delete {} {
	stop
	delete_this
}

}
