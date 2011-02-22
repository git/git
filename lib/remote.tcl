# git-gui remote management
# Copyright (C) 2006, 2007 Shawn Pearce

set some_heads_tracking 0;  # assume not

proc is_tracking_branch {name} {
	global tracking_branches
	foreach spec $tracking_branches {
		set t [lindex $spec 0]
		if {$t eq $name || [string match $t $name]} {
			return 1
		}
	}
	return 0
}

proc all_tracking_branches {} {
	global tracking_branches

	set all [list]
	set pat [list]
	set cmd [list]

	foreach spec $tracking_branches {
		set dst [lindex $spec 0]
		if {[string range $dst end-1 end] eq {/*}} {
			lappend pat $spec
			lappend cmd [string range $dst 0 end-2]
		} else {
			lappend all $spec
		}
	}

	if {$pat ne {}} {
		set fd [eval git_read for-each-ref --format=%(refname) $cmd]
		while {[gets $fd n] > 0} {
			foreach spec $pat {
				set dst [string range [lindex $spec 0] 0 end-2]
				set len [string length $dst]
				if {[string equal -length $len $dst $n]} {
					set src [string range [lindex $spec 2] 0 end-2]
					set spec [list \
						$n \
						[lindex $spec 1] \
						$src[string range $n $len end] \
						]
					lappend all $spec
				}
			}
		}
		close $fd
	}

	return [lsort -index 0 -unique $all]
}

proc load_all_remotes {} {
	global repo_config
	global all_remotes tracking_branches some_heads_tracking
	global remote_url

	set some_heads_tracking 0
	set all_remotes [list]
	set trck [list]

	set rh_str refs/heads/
	set rh_len [string length $rh_str]
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
					if {[regexp {^URL:[ 	]*(.+)$} $line line url]} {
						set remote_url($name) $url
						continue
					}
					if {![regexp {^Pull:[ 	]*([^:]+):(.+)$} \
						$line line src dst]} continue
					if {[string index $src 0] eq {+}} {
						set src [string range $src 1 end]
					}
					if {![string equal -length 5 refs/ $src]} {
						set src $rh_str$src
					}
					if {![string equal -length 5 refs/ $dst]} {
						set dst $rh_str$dst
					}
					if {[string equal -length $rh_len $rh_str $dst]} {
						set some_heads_tracking 1
					}
					lappend trck [list $dst $name $src]
				}
				close $fd
			}
		}
	}

	foreach line [array names repo_config remote.*.url] {
		if {![regexp ^remote\.(.*)\.url\$ $line line name]} continue
		lappend all_remotes $name
		set remote_url($name) $repo_config(remote.$name.url)

		if {[catch {set fl $repo_config(remote.$name.fetch)}]} {
			set fl {}
		}
		foreach line $fl {
			if {![regexp {^([^:]+):(.+)$} $line line src dst]} continue
			if {[string index $src 0] eq {+}} {
				set src [string range $src 1 end]
			}
			if {![string equal -length 5 refs/ $src]} {
				set src $rh_str$src
			}
			if {![string equal -length 5 refs/ $dst]} {
				set dst $rh_str$dst
			}
			if {[string equal -length $rh_len $rh_str $dst]} {
				set some_heads_tracking 1
			}
			lappend trck [list $dst $name $src]
		}
	}

	set tracking_branches [lsort -index 0 -unique $trck]
	set all_remotes [lsort -unique $all_remotes]
}

proc add_fetch_entry {r} {
	global repo_config
	set remote_m .mbar.remote
	set fetch_m $remote_m.fetch
	set prune_m $remote_m.prune
	set remove_m $remote_m.remove
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
		make_sure_remote_submenues_exist $remote_m

		$fetch_m add command \
			-label $r \
			-command [list fetch_from $r]
		$prune_m add command \
			-label $r \
			-command [list prune_from $r]
		$remove_m add command \
			-label $r \
			-command [list remove_remote $r]
	}
}

proc add_push_entry {r} {
	global repo_config
	set remote_m .mbar.remote
	set push_m $remote_m.push
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
		if {![winfo exists $push_m]} {
			menu $push_m
			$remote_m insert 0 cascade \
				-label [mc "Push to"] \
				-menu $push_m
		}

		$push_m add command \
			-label $r \
			-command [list push_to $r]
	}
}

proc make_sure_remote_submenues_exist {remote_m} {
	set fetch_m $remote_m.fetch
	set prune_m $remote_m.prune
	set remove_m $remote_m.remove

	if {![winfo exists $fetch_m]} {
		menu $remove_m
		$remote_m insert 0 cascade \
			-label [mc "Remove Remote"] \
			-menu $remove_m

		menu $prune_m
		$remote_m insert 0 cascade \
			-label [mc "Prune from"] \
			-menu $prune_m

		menu $fetch_m
		$remote_m insert 0 cascade \
			-label [mc "Fetch from"] \
			-menu $fetch_m
	}
}

proc update_all_remotes_menu_entry {} {
	global all_remotes

	if {[git-version < 1.6.6]} { return }

	set have_remote 0
	foreach r $all_remotes {
		incr have_remote
	}

	set remote_m .mbar.remote
	set fetch_m $remote_m.fetch
	set prune_m $remote_m.prune
	if {$have_remote > 1} {
		make_sure_remote_submenues_exist $remote_m
		if {[$fetch_m entrycget end -label] ne "All"} {

			$fetch_m insert end separator
			$fetch_m insert end command \
				-label "All" \
				-command fetch_from_all

			$prune_m insert end separator
			$prune_m insert end command \
				-label "All" \
				-command prune_from_all
		}
	} else {
		if {[winfo exists $fetch_m]} {
			if {[$fetch_m entrycget end -label] eq "All"} {

				delete_from_menu $fetch_m end
				delete_from_menu $fetch_m end

				delete_from_menu $prune_m end
				delete_from_menu $prune_m end
			}
		}
	}
}

proc populate_remotes_menu {} {
	global all_remotes

	foreach r $all_remotes {
		add_fetch_entry $r
		add_push_entry $r
	}

	update_all_remotes_menu_entry
}

proc add_single_remote {name location} {
	global all_remotes repo_config
	lappend all_remotes $name

	git remote add $name $location

	# XXX: Better re-read the config so that we will never get out
	# of sync with git remote implementation?
	set repo_config(remote.$name.url) $location
	set repo_config(remote.$name.fetch) "+refs/heads/*:refs/remotes/$name/*"

	add_fetch_entry $name
	add_push_entry $name

	update_all_remotes_menu_entry
}

proc delete_from_menu {menu name} {
	if {[winfo exists $menu]} {
		$menu delete $name
	}
}

proc remove_remote {name} {
	global all_remotes repo_config

	git remote rm $name

	catch {
		# Missing values are ok
		unset repo_config(remote.$name.url)
		unset repo_config(remote.$name.fetch)
		unset repo_config(remote.$name.push)
	}

	set i [lsearch -exact $all_remotes $name]
	set all_remotes [lreplace $all_remotes $i $i]

	set remote_m .mbar.remote
	delete_from_menu $remote_m.fetch $name
	delete_from_menu $remote_m.prune $name
	delete_from_menu $remote_m.remove $name
	# Not all remotes are in the push menu
	catch { delete_from_menu $remote_m.push $name }

	update_all_remotes_menu_entry
}
