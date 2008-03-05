# git-gui font chooser
# Copyright (C) 2007 Shawn Pearce

class choose_font {

field w
field w_family    ; # UI widget of all known family names
field w_example   ; # Example to showcase the chosen font

field f_family    ; # Currently chosen family name
field f_size      ; # Currently chosen point size

field v_family    ; # Name of global variable for family
field v_size      ; # Name of global variable for size

variable all_families [list]  ; # All fonts known to Tk

constructor pick {path title a_family a_size} {
	variable all_families

	set v_family $a_family
	set v_size $a_size

	upvar #0 $v_family pv_family
	upvar #0 $v_size pv_size

	set f_family $pv_family
	set f_size $pv_size

	make_toplevel top w
	wm title $top "[appname] ([reponame]): $title"
	wm geometry $top "+[winfo rootx $path]+[winfo rooty $path]"

	label $w.header -text $title -font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.select \
		-text [mc Select] \
		-default active \
		-command [cb _select]
	button $w.buttons.cancel \
		-text [mc Cancel] \
		-command [list destroy $w]
	pack $w.buttons.select -side right
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	frame $w.inner

	frame $w.inner.family
	label $w.inner.family.l \
		-text [mc "Font Family"] \
		-anchor w
	set w_family $w.inner.family.v
	text $w_family \
		-background white \
		-foreground black \
		-borderwidth 1 \
		-relief sunken \
		-cursor $::cursor_ptr \
		-wrap none \
		-width 30 \
		-height 10 \
		-yscrollcommand [list $w.inner.family.sby set]
	rmsel_tag $w_family
	scrollbar $w.inner.family.sby -command [list $w_family yview]
	pack $w.inner.family.l -side top -fill x
	pack $w.inner.family.sby -side right -fill y
	pack $w_family -fill both -expand 1

	frame $w.inner.size
	label $w.inner.size.l \
		-text [mc "Font Size"] \
		-anchor w
	spinbox $w.inner.size.v \
		-textvariable @f_size \
		-from 2 -to 80 -increment 1 \
		-width 3
	bind $w.inner.size.v <FocusIn> {%W selection range 0 end}
	pack $w.inner.size.l -fill x -side top
	pack $w.inner.size.v -fill x -padx 2

	grid configure $w.inner.family $w.inner.size -sticky nsew
	grid rowconfigure $w.inner 0 -weight 1
	grid columnconfigure $w.inner 0 -weight 1
	pack $w.inner -fill both -expand 1 -padx 5 -pady 5

	frame $w.example
	label $w.example.l \
		-text [mc "Font Example"] \
		-anchor w
	set w_example $w.example.t
	text $w_example \
		-background white \
		-foreground black \
		-borderwidth 1 \
		-relief sunken \
		-height 3 \
		-width 40
	rmsel_tag $w_example
	$w_example tag conf example -justify center
	$w_example insert end [mc "This is example text.\nIf you like this text, it can be your font."] example
	$w_example conf -state disabled
	pack $w.example.l -fill x
	pack $w_example -fill x
	pack $w.example -fill x -padx 5

	if {$all_families eq {}} {
		set all_families [lsort [font families]]
	}

	$w_family tag conf pick
	$w_family tag bind pick <Button-1> [cb _pick_family %x %y]\;break
	foreach f $all_families {
		set sel [list pick]
		if {$f eq $f_family} {
			lappend sel in_sel
		}
		$w_family insert end "$f\n" $sel
	}
	$w_family conf -state disabled
	_update $this

	trace add variable @f_size write [cb _update]
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Key-Return> [cb _select]\;break
	bind $w <Visibility> "
		grab $w
		focus $w
	"
	tkwait window $w
}

method _select {} {
	upvar #0 $v_family pv_family
	upvar #0 $v_size pv_size

	set pv_family $f_family
	set pv_size $f_size

	destroy $w
}

method _pick_family {x y} {
	variable all_families

	set i [lindex [split [$w_family index @$x,$y] .] 0]
	set n [lindex $all_families [expr {$i - 1}]]
	if {$n ne {}} {
		$w_family tag remove in_sel 0.0 end
		$w_family tag add in_sel $i.0 [expr {$i + 1}].0
		set f_family $n
		_update $this
	}
}

method _update {args} {
	variable all_families

	set i [lsearch -exact $all_families $f_family]
	if {$i < 0} return

	$w_example tag conf example -font [list $f_family $f_size]
	$w_family see [expr {$i + 1}].0
}

}
