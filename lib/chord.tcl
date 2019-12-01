# Simple Chord for Tcl
#
# A "chord" is a method with more than one entrypoint and only one body, such
# that the body runs only once all the entrypoints have been called by
# different asynchronous tasks. In this implementation, the chord is defined
# dynamically for each invocation. A SimpleChord object is created, supplying
# body script to be run when the chord is completed, and then one or more notes
# are added to the chord. Each note can be called like a proc, and returns
# immediately if the chord isn't yet complete. When the last remaining note is
# called, the body runs before the note returns.
#
# The SimpleChord class has a constructor that takes the body script, and a
# method add_note that returns a note object. Since the body script does not
# run in the context of the procedure that defined it, a mechanism is provided
# for injecting variables into the chord for use by the body script. The
# activation of a note is idempotent; multiple calls have the same effect as
# a simple call.
#
# If you are invoking asynchronous operations with chord notes as completion
# callbacks, and there is a possibility that earlier operations could complete
# before later ones are started, it is a good practice to create a "common"
# note on the chord that prevents it from being complete until you're certain
# you've added all the notes you need.
#
# Example:
#
#   # Turn off the UI while running a couple of async operations.
#   lock_ui
#
#   set chord [SimpleChord new {
#     unlock_ui
#     # Note: $notice here is not referenced in the calling scope
#     if {$notice} { info_popup $notice }
#   }
#
#   # Configure a note to keep the chord from completing until
#   # all operations have been initiated.
#   set common_note [$chord add_note]
#
#   # Pass notes as 'after' callbacks to other operations
#   async_operation $args [$chord add_note]
#   other_async_operation $args [$chord add_note]
#
#   # Communicate with the chord body
#   if {$condition} {
#     # This sets $notice in the same context that the chord body runs in.
#     $chord eval { set notice "Something interesting" }
#   }
#
#   # Activate the common note, making the chord eligible to complete
#   $common_note
#
# At this point, the chord will complete at some unknown point in the future.
# The common note might have been the first note activated, or the async
# operations might have completed synchronously and the common note is the
# last one, completing the chord before this code finishes, or anything in
# between. The purpose of the chord is to not have to worry about the order.

# SimpleChord class:
#   Represents a procedure that conceptually has multiple entrypoints that must
#   all be called before the procedure executes. Each entrypoint is called a
#   "note". The chord is only "completed" when all the notes are "activated".
oo::class create SimpleChord {
	variable notes body is_completed

	# Constructor:
	#   set chord [SimpleChord new {body}]
	#     Creates a new chord object with the specified body script. The
	#     body script is evaluated at most once, when a note is activated
	#     and the chord has no other non-activated notes.
	constructor {body} {
		set notes [list]
		my eval [list set body $body]
		set is_completed 0
	}

	# Method:
	#   $chord eval {script}
	#     Runs the specified script in the same context (namespace) in which
	#     the chord body will be evaluated. This can be used to set variable
	#     values for the chord body to use.
	method eval {script} {
		namespace eval [self] $script
	}

	# Method:
	#   set note [$chord add_note]
	#     Adds a new note to the chord, an instance of ChordNote. Raises an
	#     error if the chord is already completed, otherwise the chord is
	#     updated so that the new note must also be activated before the
	#     body is evaluated.
	method add_note {} {
		if {$is_completed} { error "Cannot add a note to a completed chord" }

		set note [ChordNote new [self]]

		lappend notes $note

		return $note
	}

	# This method is for internal use only and is intentionally undocumented.
	method notify_note_activation {} {
		if {!$is_completed} {
			foreach note $notes {
				if {![$note is_activated]} { return }
			}

			set is_completed 1

			namespace eval [self] $body
			namespace delete [self]
		}
	}
}

# ChordNote class:
#   Represents a note within a chord, providing a way to activate it. When the
#   final note of the chord is activated (this can be any note in the chord,
#   with all other notes already previously activated in any order), the chord's
#   body is evaluated.
oo::class create ChordNote {
	variable chord is_activated

	# Constructor:
	#   Instances of ChordNote are created internally by calling add_note on
	#   SimpleChord objects.
	constructor {chord} {
		my eval set chord $chord
		set is_activated 0
	}

	# Method:
	#   [$note is_activated]
	#     Returns true if this note has already been activated.
	method is_activated {} {
		return $is_activated
	}

	# Method:
	#   $note
	#     Activates the note, if it has not already been activated, and
	#     completes the chord if there are no other notes awaiting
	#     activation. Subsequent calls will have no further effect.
	#
	# NB: In TclOO, if an object is invoked like a method without supplying
	#     any method name, then this internal method `unknown` is what
	#     actually runs (with no parameters). It is used in the ChordNote
	#     class for the purpose of allowing the note object to be called as
	#     a function (see example above). (The `unknown` method can also be
	#     used to support dynamic dispatch, but must take parameters to
	#     identify the "unknown" method to be invoked. In this form, this
	#     proc serves only to make instances behave directly like methods.)
	method unknown {} {
		if {!$is_activated} {
			set is_activated 1
			$chord notify_note_activation
		}
	}
}
