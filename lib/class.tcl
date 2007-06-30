# git-gui simple class/object fake-alike
# Copyright (C) 2007 Shawn Pearce

proc class {class body} {
	if {[namespace exists $class]} {
		error "class $class already declared"
	}
	namespace eval $class "
		variable __nextid     0
		variable __sealed     0
		variable __field_list {}
		variable __field_array

		proc cb {name args} {
			upvar this this
			concat \[list ${class}::\$name \$this\] \$args
		}
	"
	namespace eval $class $body
}

proc field {name args} {
	set class [uplevel {namespace current}]
	variable ${class}::__sealed
	variable ${class}::__field_array

	switch [llength $args] {
	0 { set new [list $name] }
	1 { set new [list $name [lindex $args 0]] }
	default { error "wrong # args: field name value?" }
	}

	if {$__sealed} {
		error "class $class is sealed (cannot add new fields)"
	}

	if {[catch {set old $__field_array($name)}]} {
		variable ${class}::__field_list
		lappend __field_list $new
		set __field_array($name) 1
	} else {
		error "field $name already declared"
	}
}

proc constructor {name params body} {
	set class [uplevel {namespace current}]
	set ${class}::__sealed 1
	variable ${class}::__field_list
	set mbodyc {}

	append mbodyc {set this } $class
	append mbodyc {::__o[incr } $class {::__nextid]::__d} \;
	append mbodyc {create_this } $class \;
	append mbodyc {set __this [namespace qualifiers $this]} \;

	if {$__field_list ne {}} {
		append mbodyc {upvar #0}
		foreach n $__field_list {
			set n [lindex $n 0]
			append mbodyc { ${__this}::} $n { } $n
			regsub -all @$n\\M $body "\${__this}::$n" body
		}
		append mbodyc \;
		foreach n $__field_list {
			if {[llength $n] == 2} {
				append mbodyc \
				{set } [lindex $n 0] { } [list [lindex $n 1]] \;
			}
		}
	}
	append mbodyc $body
	namespace eval $class [list proc $name $params $mbodyc]
}

proc method {name params body {deleted {}} {del_body {}}} {
	set class [uplevel {namespace current}]
	set ${class}::__sealed 1
	variable ${class}::__field_list
	set params [linsert $params 0 this]
	set mbodyc {}

	append mbodyc {set __this [namespace qualifiers $this]} \;

	switch $deleted {
	{} {}
	ifdeleted {
		append mbodyc {if {![namespace exists $__this]} }
		append mbodyc \{ $del_body \; return \} \;
	}
	default {
		error "wrong # args: method name args body (ifdeleted body)?"
	}
	}

	set decl {}
	foreach n $__field_list {
		set n [lindex $n 0]
		if {[regexp -- $n\\M $body]} {
			if {   [regexp -all -- $n\\M $body] == 1
				&& [regexp -all -- \\\$$n\\M $body] == 1
				&& [regexp -all -- \\\$$n\\( $body] == 0} {
				regsub -all \
					\\\$$n\\M $body \
					"\[set \${__this}::$n\]" body
			} else {
				append decl { ${__this}::} $n { } $n
				regsub -all @$n\\M $body "\${__this}::$n" body
			}
		}
	}
	if {$decl ne {}} {
		append mbodyc {upvar #0} $decl \;
	}
	append mbodyc $body
	namespace eval $class [list proc $name $params $mbodyc]
}

proc create_this {class} {
	upvar this this
	namespace eval [namespace qualifiers $this] [list proc \
		[namespace tail $this] \
		[list name args] \
		"eval \[list ${class}::\$name $this\] \$args" \
	]
}

proc delete_this {{t {}}} {
	if {$t eq {}} {
		upvar this this
		set t $this
	}
	set t [namespace qualifiers $t]
	if {[namespace exists $t]} {namespace delete $t}
}

proc make_toplevel {t w args} {
	upvar $t top $w pfx this this

	if {[llength $args] % 2} {
		error "make_toplevel topvar winvar {options}"
	}
	set autodelete 1
	foreach {name value} $args {
		switch -exact -- $name {
		-autodelete {set autodelete $value}
		default     {error "unsupported option $name"}
		}
	}

	if {[winfo ismapped .]} {
		regsub -all {::} $this {__} w
		set top .$w
		set pfx $top
		toplevel $top
	} else {
		set top .
		set pfx {}
	}

	if {$autodelete} {
		wm protocol $top WM_DELETE_WINDOW "
			[list delete_this $this]
			[list destroy $top]
		"
	}
}


## auto_mkindex support for class/constructor/method
##
auto_mkindex_parser::command class {name body} {
	variable parser
	variable contextStack
	set contextStack [linsert $contextStack 0 $name]
	$parser eval [list _%@namespace eval $name] $body
	set contextStack [lrange $contextStack 1 end]
}
auto_mkindex_parser::command constructor {name args} {
	variable index
	variable scriptFile
	append index [list set auto_index([fullname $name])] \
		[format { [list source [file join $dir %s]]} \
		[file split $scriptFile]] "\n"
}
