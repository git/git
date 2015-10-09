Milestone 3
===========

Due: 2015/10/08 at 11:59pm
Points: 2

3a. By now, each team member should have selected something specific
to work on. It could be:

- to add a new feature,
- to optimize a function (for time or memory efficiency), or
- to fix a problem.

In any case, it should relate somehow to a data structure or algorithm
covered in class. For each,

- State who will work on this piece.
- Describe what you want to do. If it's a new feature: What is it and
  why is it needed? If it's something inefficient or broken: What is
  it and how is it currently implemented? What is its asymptotic
  time/space complexity? What's wrong with it?
- Describe how this piece interfaces (or will interface) with the rest
  of the code. If it's a function, what are the pre- and
  post-conditions? If it's a class, what are the pre- and
  post-conditions of its public member functions?

Breanna Devore-McDonald:
Keith Kinnard: Adding a repair function to the git fsck command.  The repair function would fetch uncorrupted versions of objects from another repository when git fsck finds missing or uncorrupted objects. This feature would be useful because then a user would not have to go through the directory manually and fix the problem or find an older version of the corrupted file. 
This function will be called if git fsck comes across a missing or corrupted file.  The function will need to take in the current repository as well as the name of the corrupted or missing file. It will then search for an older version of that file in either a previous version of the current repository or in a parent repository.  If it does not find one, it will return false and let the user know that an older version of the file was not found.  Otherwise, it will return true and correct the issue.

Nancy McNamara: I will be working to enhance git’s autocorrect feature. At this time, when a command is entered that git does not recognize, it computes the Levenshtein distance between the known commands and the entered words, and if the distance falls below the similarity floor then those possible commands are printed to the user. I would like to add the functionality that the suggested options will be printed with corresponding numbers in order of most past uses by the particular user and entering one of these numbers will execute the appropriate command while entering a 0 will indicate that none of the printed commands are desired. In order for this to happen, I plan to constantly quickly re-order the list of commands that the git program uses every time a command is run in order to keep them in popuarity order. This functionality will hopefully be an option that can be turned on using a "git-config —global help.orderedAutocorrect” type format.

Elliott Runburg:

3b. You should also be in contact with the developer community (ask
your TA for tips on how to do this successfully). Cut and paste an
excerpt from one of your conversations.
