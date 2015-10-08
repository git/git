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

Nancy McNamara:
Elliott Runburg:

3b. You should also be in contact with the developer community (ask
your TA for tips on how to do this successfully). Cut and paste an
excerpt from one of your conversations.
