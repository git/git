#!/usr/bin/env kotlin

/**
 * Copies contents of the release notes file created/modified
 * in this commit to a new file to be used by the workflow.
 */

import java.io.File

println("Files modified in this commit:")
args.forEachIndexed { index, name ->
    println("\t${index + 1}- $name")
}

val notesFile = args
    .map(::File)
    .singleOrNull { "RelNotes" in it.parent }

notesFile
    ?.copyTo(File("changelog.txt"))
    ?: println("No release notes file modified in this commit")
