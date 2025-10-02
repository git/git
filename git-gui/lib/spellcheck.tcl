# git-gui spellchecking support through ispell/aspell
# Copyright (C) 2008 Shawn Pearce

class spellcheck {

field s_fd      {} ; # pipe to ispell/aspell
field s_version {} ; # ispell/aspell version string
field s_lang    {} ; # current language code
field s_prog aspell; # are we actually old ispell?
field s_failed   0 ; # is $s_prog bogus and not working?

field w_text      ; # text widget we are spelling
field w_menu      ; # context menu for the widget
field s_menuidx 0 ; # last index of insertion into $w_menu

field s_i           {} ; # timer registration for _run callbacks
field s_clear        0 ; # did we erase misspelled tags yet?
field s_seen    [list] ; # lines last seen from $w_text in _run
field s_checked [list] ; # lines already checked
field s_pending [list] ; # [$line $data] sent to ispell/aspell
field s_suggest        ; # array, list of suggestions, keyed by misspelling

constructor init {pipe_fd ui_text ui_menu} {
	set w_text $ui_text
	set w_menu $ui_menu
	array unset s_suggest

	bind_button3 $w_text [cb _popup_suggest %X %Y @%x,%y]
	_connect $this $pipe_fd
	return $this
}

method _connect {pipe_fd} {
	fconfigure $pipe_fd \
		-encoding utf-8 \
		-translation lf

	if {[gets $pipe_fd s_version] <= 0} {
		if {[catch {close $pipe_fd} err]} {

			# Eh?  Is this actually ispell choking on aspell options?
			#
			if {$s_prog eq {aspell}
				&& [regexp -nocase {^Usage: } $err]
				&& ![catch {
						set pipe_fd [open [list | $s_prog -v] r]
						gets $pipe_fd s_version
						close $pipe_fd
				}]
				&& $s_version ne {}} {
				if {{@(#) } eq [string range $s_version 0 4]} {
					set s_version [string range $s_version 5 end]
				}
				set s_failed 1
				error_popup [strcat \
					[mc "Unsupported spell checker"] \
					":\n\n$s_version"]
				set s_version {}
				return
			}

			regsub -nocase {^Error: } $err {} err
			if {$s_fd eq {}} {
				error_popup [strcat [mc "Spell checking is unavailable"] ":\n\n$err"]
			} else {
				error_popup [strcat \
					[mc "Invalid spell checking configuration"] \
					":\n\n$err\n\n" \
					[mc "Reverting dictionary to %s." $s_lang]]
			}
		} else {
			error_popup [mc "Spell checker silently failed on startup"]
		}
		return
	}

	if {{@(#) } ne [string range $s_version 0 4]} {
		catch {close $pipe_fd}
		error_popup [strcat [mc "Unrecognized spell checker"] ":\n\n$s_version"]
		return
	}
	set s_version [string range [string trim $s_version] 5 end]
	regexp \
		{International Ispell Version .* \(but really (Aspell .*?)\)$} \
		$s_version _junk s_version
	regexp {^Aspell (\d)+\.(\d+)} $s_version _junk major minor

	puts $pipe_fd !             ; # enable terse mode

	# fetch the language
	if {$major > 0 || ($major == 0 && $minor >= 60)} {
		puts $pipe_fd {$$cr master}
		flush $pipe_fd
		gets $pipe_fd s_lang
		regexp {[/\\]([^/\\]+)\.[^\.]+$} $s_lang _ s_lang
	} else {
		set s_lang {}
	}

	if {$::default_config(gui.spellingdictionary) eq {}
	 && [get_config gui.spellingdictionary] eq {}} {
		set ::default_config(gui.spellingdictionary) $s_lang
	}

	if {$s_fd ne {}} {
		catch {close $s_fd}
	}
	set s_fd $pipe_fd

	fconfigure $s_fd -blocking 0
	fileevent $s_fd readable [cb _read]

	$w_text tag conf misspelled \
		-foreground red \
		-underline 1

	array unset s_suggest
	set s_seen    [list]
	set s_checked [list]
	set s_pending [list]
	_run $this
}

method lang {{n {}}} {
	if {$n ne {} && $s_lang ne $n && !$s_failed} {
		set spell_cmd [list |]
		lappend spell_cmd aspell
		lappend spell_cmd --master=$n
		lappend spell_cmd --mode=none
		lappend spell_cmd --encoding=UTF-8
		lappend spell_cmd pipe
		_connect $this [open $spell_cmd r+]
	}
	return $s_lang
}

method version {} {
	if {$s_version ne {}} {
		return "$s_version, $s_lang"
	}
	return {}
}

method stop {} {
	while {$s_menuidx > 0} {
		$w_menu delete 0
		incr s_menuidx -1
	}
	$w_text tag delete misspelled

	catch {close $s_fd}
	catch {after cancel $s_i}
	set s_fd {}
	set s_i {}
	set s_lang {}
}

method _popup_suggest {X Y pos} {
	while {$s_menuidx > 0} {
		$w_menu delete 0
		incr s_menuidx -1
	}

	set b_loc [$w_text index "$pos wordstart"]
	set e_loc [_wordend $this $b_loc]
	set orig  [$w_text get $b_loc $e_loc]
	set tags  [$w_text tag names $b_loc]

	if {[lsearch -exact $tags misspelled] >= 0} {
		if {[info exists s_suggest($orig)]} {
			set cnt 0
			foreach s $s_suggest($orig) {
				if {$cnt < 5} {
					$w_menu insert $s_menuidx command \
						-label $s \
						-command [cb _replace $b_loc $e_loc $s]
					incr s_menuidx
					incr cnt
				} else {
					break
				}
			}
		} else {
			$w_menu insert $s_menuidx command \
				-label [mc "No Suggestions"] \
				-state disabled
			incr s_menuidx
		}
		$w_menu insert $s_menuidx separator
		incr s_menuidx
	}

	$w_text mark set saved-insert insert
	tk_popup $w_menu $X $Y
}

method _replace {b_loc e_loc word} {
	$w_text configure -autoseparators 0
	$w_text edit separator

	$w_text delete $b_loc $e_loc
	$w_text insert $b_loc $word

	$w_text edit separator
	$w_text configure -autoseparators 1
	$w_text mark set insert saved-insert
}

method _restart_timer {} {
	set s_i [after 300 [cb _run]]
}

proc _match_length {max_line arr_name} {
	upvar $arr_name a

	if {[llength $a] > $max_line} {
		set a [lrange $a 0 $max_line]
	}
	while {[llength $a] <= $max_line} {
		lappend a {}
	}
}

method _wordend {pos} {
	set pos  [$w_text index "$pos wordend"]
	set tags [$w_text tag names $pos]
	while {[lsearch -exact $tags misspelled] >= 0} {
		set pos  [$w_text index "$pos +1c"]
		set tags [$w_text tag names $pos]
	}
	return $pos
}

method _run {} {
	set cur_pos  [$w_text index {insert -1c}]
	set cur_line [lindex [split $cur_pos .] 0]
	set max_line [lindex [split [$w_text index end] .] 0]
	_match_length $max_line s_seen
	_match_length $max_line s_checked

	# Nothing in the message buffer?  Nothing to spellcheck.
	#
	if {$cur_line == 1
	 && $max_line == 2
	 && [$w_text get 1.0 end] eq "\n"} {
		array unset s_suggest
		_restart_timer $this
		return
	}

	set active 0
	for {set n 1} {$n <= $max_line} {incr n} {
		set s [$w_text get "$n.0" "$n.end"]

		# Don't spellcheck the current line unless we are at
		# a word boundary.  The user might be typing on it.
		#
		if {$n == $cur_line
		 && ![regexp {^\W$} [$w_text get $cur_pos insert]]} {

			# If the current word is misspelled remove the tag
			# but force a spellcheck later.
			#
			set tags [$w_text tag names $cur_pos]
			if {[lsearch -exact $tags misspelled] >= 0} {
				$w_text tag remove misspelled \
					"$cur_pos wordstart" \
					[_wordend $this $cur_pos]
				lset s_seen    $n $s
				lset s_checked $n {}
			}

			continue
		}

		if {[lindex $s_seen    $n] eq $s
		 && [lindex $s_checked $n] ne $s} {
			# Don't send empty lines to Aspell it doesn't check them.
			#
			if {$s eq {}} {
				lset s_checked $n $s
				continue
			}

			# Don't send typical s-b-o lines as the emails are
			# almost always misspelled according to Aspell.
			#
			if {[regexp -nocase {^[a-z-]+-by:.*<.*@.*>$} $s]} {
				$w_text tag remove misspelled "$n.0" "$n.end"
				lset s_checked $n $s
				continue
			}

			puts $s_fd ^$s
			lappend s_pending [list $n $s]
			set active 1
		} else {
			# Delay until another idle loop to make sure we don't
			# spellcheck lines the user is actively changing.
			#
			lset s_seen $n $s
		}
	}

	if {$active} {
		set s_clear 1
		flush $s_fd
	} else {
		_restart_timer $this
	}
}

method _read {} {
	while {[gets $s_fd line] >= 0} {
		set lineno [lindex $s_pending 0 0]
		set line [string trim $line]

		if {$s_clear} {
			$w_text tag remove misspelled "$lineno.0" "$lineno.end"
			set s_clear 0
		}

		if {$line eq {}} {
			lset s_checked $lineno [lindex $s_pending 0 1]
			set s_pending [lrange $s_pending 1 end]
			set s_clear 1
			continue
		}

		set sugg [list]
		switch -- [string range $line 0 1] {
		{& } {
			set line [split [string range $line 2 end] :]
			set info [split [lindex $line 0] { }]
			set orig [lindex $info 0]
			set offs [lindex $info 2]
			foreach s [split [lindex $line 1] ,] {
				lappend sugg [string range $s 1 end]
			}
		}
		{# } {
			set info [split [string range $line 2 end] { }]
			set orig [lindex $info 0]
			set offs [lindex $info 1]
		}
		default {
			puts stderr "<spell> $line"
			continue
		}
		}

		incr offs -1
		set b_loc "$lineno.$offs"
		set e_loc [$w_text index "$lineno.$offs wordend"]
		set curr [$w_text get $b_loc $e_loc]

		# At least for English curr = "bob", orig = "bob's"
		# so Tk didn't include the 's but Aspell did.  We
		# try to round out the word.
		#
		while {$curr ne $orig
		 && [string equal -length [string length $curr] $curr $orig]} {
			set n_loc  [$w_text index "$e_loc +1c"]
			set n_curr [$w_text get $b_loc $n_loc]
			if {$n_curr eq $curr} {
				break
			}
			set curr  $n_curr
			set e_loc $n_loc
		}

		if {$curr eq $orig} {
			$w_text tag add misspelled $b_loc $e_loc
			if {[llength $sugg] > 0} {
				set s_suggest($orig) $sugg
			} else {
				unset -nocomplain s_suggest($orig)
			}
		} else {
			unset -nocomplain s_suggest($orig)
		}
	}

	fconfigure $s_fd -block 1
	if {[eof $s_fd]} {
		if {![catch {close $s_fd} err]} {
			set err [mc "Unexpected EOF from spell checker"]
		}
		catch {after cancel $s_i}
		$w_text tag remove misspelled 1.0 end
		error_popup [strcat [mc "Spell Checker Failed"] "\n\n" $err]
		return
	}
	fconfigure $s_fd -block 0

	if {[llength $s_pending] == 0} {
		_restart_timer $this
	}
}

proc available_langs {} {
	set langs [list]
	catch {
		set fd [open [list | aspell dump dicts] r]
		while {[gets $fd line] >= 0} {
			if {$line eq {}} continue
			lappend langs $line
		}
		close $fd
	}
	return $langs
}

}
