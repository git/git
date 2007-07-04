# git-gui revision chooser
# Copyright (C) 2006, 2007 Shawn Pearce

class choose_rev {

field w               ; # our megawidget path
field revtype       {}; # type of revision chosen

field c_head        {}; # selected local branch head
field c_trck        {}; # selected tracking branch
field c_tag         {}; # selected tag
field c_expr        {}; # current revision expression

constructor new {path {title {}}} {
	global all_heads current_branch

	set w $path

	if {$title ne {}} {
		labelframe $w -text $title
	} else {
		frame $w
	}
	bind $w <Destroy> [cb _delete %W]

	if {$all_heads ne {}} {
		set c_head $current_branch
		radiobutton $w.head_r \
			-text {Local Branch:} \
			-value head \
			-variable @revtype
		eval tk_optionMenu $w.head_m @c_head $all_heads
		grid $w.head_r $w.head_m -sticky w
		if {$revtype eq {}} {
			set revtype head
		}
		trace add variable @c_head write [cb _select head]
	}

	set all_trackings [all_tracking_branches]
	if {$all_trackings ne {}} {
		set c_trck [lindex $all_trackings 0]
		radiobutton $w.trck_r \
			-text {Tracking Branch:} \
			-value trck \
			-variable @revtype
		eval tk_optionMenu $w.trck_m @c_trck $all_trackings
		grid $w.trck_r $w.trck_m -sticky w
		if {$revtype eq {}} {
			set revtype trck
		}
		trace add variable @c_trck write [cb _select trck]
	}

	set all_tags [load_all_tags]
	if {$all_tags ne {}} {
		set c_tag [lindex $all_tags 0]
		radiobutton $w.tag_r \
			-text {Tag:} \
			-value tag \
			-variable @revtype
		eval tk_optionMenu $w.tag_m @c_tag $all_tags
		grid $w.tag_r $w.tag_m -sticky w
		if {$revtype eq {}} {
			set revtype tag
		}
		trace add variable @c_tag write [cb _select tag]
	}

	radiobutton $w.expr_r \
		-text {Revision Expression:} \
		-value expr \
		-variable @revtype
	entry $w.expr_t \
		-borderwidth 1 \
		-relief sunken \
		-width 50 \
		-textvariable @c_expr \
		-validate key \
		-validatecommand [cb _validate %d %S]
	grid $w.expr_r $w.expr_t -sticky we -padx {0 5}
	if {$revtype eq {}} {
		set revtype expr
	}

	grid columnconfigure $w 1 -weight 1
	return $this
}

method get {} {
	switch -- $revtype {
	head { return $c_head }
	trck { return $c_trck }
	tag  { return $c_tag  }
	expr { return $c_expr }
	default { error "unknown type of revision" }
	}
}

method get_commit {} {
	set rev [get $this]
	return [git rev-parse --verify "${rev}^0"]
}

method _validate {d S} {
	if {$d == 1} {
		if {[regexp {\s} $S]} {
			return 0
		}
		if {[string length $S] > 0} {
			set revtype expr
		}
	}
	return 1
}

method _select {value args} {
	set revtype $value
}

method _delete {current} {
	if {$current eq $w} {
		delete_this
	}
}

}
