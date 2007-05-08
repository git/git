# git-gui remote management
# Copyright (C) 2006, 2007 Shawn Pearce

proc is_tracking_branch {name} {
	global tracking_branches

	if {![catch {set info $tracking_branches($name)}]} {
		return 1
	}
	foreach t [array names tracking_branches] {
		if {[string match {*/\*} $t] && [string match $t $name]} {
			return 1
		}
	}
	return 0
}

proc all_tracking_branches {} {
	global tracking_branches

	set all_trackings {}
	set cmd {}
	foreach name [array names tracking_branches] {
		if {[regsub {/\*$} $name {} name]} {
			lappend cmd $name
		} else {
			regsub ^refs/(heads|remotes)/ $name {} name
			lappend all_trackings $name
		}
	}

	if {$cmd ne {}} {
		set fd [open "| git for-each-ref --format=%(refname) $cmd" r]
		while {[gets $fd name] > 0} {
			regsub ^refs/(heads|remotes)/ $name {} name
			lappend all_trackings $name
		}
		close $fd
	}

	return [lsort -unique $all_trackings]
}

proc load_all_remotes {} {
	global repo_config
	global all_remotes tracking_branches

	set all_remotes [list]
	array unset tracking_branches

	set rm_dir [gitdir remotes]
	if {[file isdirectory $rm_dir]} {
		set all_remotes [glob \
			-types f \
			-tails \
			-nocomplain \
			-directory $rm_dir *]

		foreach name $all_remotes {
			catch {
				set fd [open [file join $rm_dir $name] r]
				while {[gets $fd line] >= 0} {
					if {![regexp {^Pull:[ 	]*([^:]+):(.+)$} \
						$line line src dst]} continue
					if {![regexp ^refs/ $dst]} {
						set dst "refs/heads/$dst"
					}
					set tracking_branches($dst) [list $name $src]
				}
				close $fd
			}
		}
	}

	foreach line [array names repo_config remote.*.url] {
		if {![regexp ^remote\.(.*)\.url\$ $line line name]} continue
		lappend all_remotes $name

		if {[catch {set fl $repo_config(remote.$name.fetch)}]} {
			set fl {}
		}
		foreach line $fl {
			if {![regexp {^([^:]+):(.+)$} $line line src dst]} continue
			if {![regexp ^refs/ $dst]} {
				set dst "refs/heads/$dst"
			}
			set tracking_branches($dst) [list $name $src]
		}
	}

	set all_remotes [lsort -unique $all_remotes]
}

proc populate_fetch_menu {} {
	global all_remotes repo_config

	set m .mbar.fetch
	foreach r $all_remotes {
		set enable 0
		if {![catch {set a $repo_config(remote.$r.url)}]} {
			if {![catch {set a $repo_config(remote.$r.fetch)}]} {
				set enable 1
			}
		} else {
			catch {
				set fd [open [gitdir remotes $r] r]
				while {[gets $fd n] >= 0} {
					if {[regexp {^Pull:[ \t]*([^:]+):} $n]} {
						set enable 1
						break
					}
				}
				close $fd
			}
		}

		if {$enable} {
			$m add command \
				-label "Fetch from $r..." \
				-command [list fetch_from $r]
		}
	}
}

proc populate_push_menu {} {
	global all_remotes repo_config

	set m .mbar.push
	set fast_count 0
	foreach r $all_remotes {
		set enable 0
		if {![catch {set a $repo_config(remote.$r.url)}]} {
			if {![catch {set a $repo_config(remote.$r.push)}]} {
				set enable 1
			}
		} else {
			catch {
				set fd [open [gitdir remotes $r] r]
				while {[gets $fd n] >= 0} {
					if {[regexp {^Push:[ \t]*([^:]+):} $n]} {
						set enable 1
						break
					}
				}
				close $fd
			}
		}

		if {$enable} {
			if {!$fast_count} {
				$m add separator
			}
			$m add command \
				-label "Push to $r..." \
				-command [list push_to $r]
			incr fast_count
		}
	}
}
