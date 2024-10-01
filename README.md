[![Build Status](https://github.com/git/git/workflows/CI/badge.svg)](https://github.com/git/git/actions?query=branch%3Amaster+event%3Apush)

# Git - Fast, Scalable, Distributed Revision Control System

Git is a powerful, scalable, distributed revision control system with an
extensive command set that supports both high-level operations and direct access to its internals.

### License and Origin
Git is an open-source project covered by the GNU General Public License version 2. 
Some parts are under different licenses compatible with GPLv2. It was originally created 
by Linus Torvalds with contributions from a community of developers.

### Installation Instructions
For installation instructions, please see the file [INSTALL][].

### Resources and Documentation
A wealth of online resources for Git can be found at [git-scm.com](https://git-scm.com/), 
including full documentation and various Git-related tools.

To get started:
- Read [Documentation/gittutorial.txt][].
- For a practical set of commands, see [Documentation/giteveryday.txt][].
- Detailed documentation for each command can be found in `Documentation/git-<commandname>.txt`.

Once Git is correctly installed, you can access the tutorial by running:
- `man gittutorial` or `git help tutorial`.

For specific commands:
- `man git-<commandname>` or `git help <commandname>`.

### CVS Users
CVS users transitioning to Git may find [Documentation/gitcvs-migration.txt][] helpful. 
You can also view it using:
- `man gitcvs-migration` or `git help cvs-migration`.

### Mailing Lists and Community
Development and discussion of Git take place on the Git mailing list. 
Feel free to post bug reports, feature requests, comments, or patches to:
- `git@vger.kernel.org`

For patch submissions, see [Documentation/SubmittingPatches][].  
For coding guidelines, see [Documentation/CodingGuidelines][].

### Translations (Localization)
Those interested in contributing to translations (l10n) should refer to [po/README.md][] 
for more information on handling `po` files (Portable Object files).

### Subscribe to the Mailing List
To subscribe to the mailing list, send an email to:
- `git+subscribe@vger.kernel.org`

For further details, visit:  
- <https://subspace.kernel.org/subscribing.html>

Mailing list archives are available at:  
- <https://lore.kernel.org/git/>  
- <https://marc.info/?l=git>

### Security Disclosures
For security-related issues, please report them privately to the Git Security mailing list:
- `git-security@googlegroups.com`

### "What's Cooking" Reports
The maintainer regularly sends "What's cooking" reports to the mailing list, summarizing the status of various development topics. These discussions provide valuable insight into the project's direction and upcoming tasks.

### The Name "Git"
The name "Git" was chosen by Linus Torvalds when he first developed the system. It can be interpreted in different ways:
- **Random three-letter combination**: Pronounceable and not used by any common UNIX command. The fact it resembles "get" may or may not be intentional.
- **"Stupid, contemptible, despicable"**: Or simply, "stupid" (depending on your mood).
- **"Global Information Tracker"**: When things are going well, Git can seem like a miracle.
- **"Goddamn Idiotic Truckload of Sh*t"**: When things go wrong.

---

[INSTALL]: INSTALL  
[Documentation/gittutorial.txt]: Documentation/gittutorial.txt  
[Documentation/giteveryday.txt]: Documentation/giteveryday.txt  
[Documentation/gitcvs-migration.txt]: Documentation/gitcvs-migration.txt  
[Documentation/SubmittingPatches]: Documentation/SubmittingPatches  
[Documentation/CodingGuidelines]: Documentation/CodingGuidelines  
[po/README.md]: po/README.md
