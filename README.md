[![Build Status](https://github.com/git/git/workflows/CI/badge.svg)](https://github.com/git/git/actions?query=branch%3Amaster+event%3Apush)

# Git - Fast, Scalable, Distributed Revision Control System

Git is a robust, scalable, and distributed revision control system created to handle projects of all sizes efficiently. It provides a combination of high-level operations and deep internal access, making it adaptable to a wide variety of workflows and project demands.

## Features
Git supports:
- **Branching and Merging**: Powerful branching and merging functionalities allow multiple developers to work independently and integrate their work easily.
- **Data Integrity**: Every file and commit is checksummed and referenced by that checksum. Once it’s committed, it is impossible to change the file or the commit itself without Git knowing.
- **Distributed Workflow**: Git allows for a fully distributed workflow. Every developer has a local copy of the entire project history.
- **Staging Area**: Prepares files to be committed to the project, providing an intermediate stage before actually saving them to the repository.
- **Commit History**: Every version of your project is stored with its exact state and changes recorded over time.

## License
Git is licensed under the GNU General Public License version 2 (GPLv2). Some portions of the software may fall under other licenses that are compatible with GPLv2. This ensures Git remains open-source and available to developers around the world.

## Installation
Installation instructions for various platforms (Linux, macOS, Windows) are available in the `INSTALL` file within the source code or by visiting the [Git official site](https://git-scm.com/). For detailed setup instructions:

- Follow the platform-specific instructions listed in `INSTALL`.
- For macOS users, Git is pre-installed, or you can install it via Homebrew (`brew install git`).
- For Windows, download the installer from the Git website, or use package managers like `choco install git` or `scoop install git`.

## Documentation
For complete documentation, please refer to [git-scm.com](https://git-scm.com/). Documentation is also available within the repository and in the installed files. Here are a few key documents to get started:

- **Tutorial**: Basic introduction and steps to start using Git:  
  `man gittutorial`  
  or  
  `git help tutorial`
  
- **Everyday Git**: A guide to using Git for common daily tasks:  
  `man giteveryday`  
  or  
  `git help everyday`

- **Command Reference**: To learn more about any Git command:  
  `man git-<command>`  
  or  
  `git help <command>`

### Migrating from CVS
For users migrating from CVS, Git provides extensive help to ease the transition. The migration guide can be accessed via:

- `man gitcvs-migration`  
  or  
  `git help cvs-migration`

Documentation related to CVS migration is found in the `Documentation/gitcvs-migration.txt` file.

## Mailing List and Community Involvement
Git has an active and growing developer community. You can join the Git mailing list to discuss development topics, report bugs, or submit patches. To subscribe, send an email to:

- `git+subscribe@vger.kernel.org`

Active discussions take place on the mailing list, which is a good starting point for both new contributors and seasoned developers. Mailing list archives can be found at:

- <https://lore.kernel.org/git/>
- <https://marc.info/?l=git>

For details on how to submit patches, refer to the `Documentation/SubmittingPatches` file. Patch submission guidelines can be found in `Documentation/CodingGuidelines`.

### Contribution Guidelines
We encourage contributions! If you're interested in helping out, please refer to our coding guidelines in `Documentation/CodingGuidelines`. There is also a section on patch submission in `Documentation/SubmittingPatches`. Make sure you follow these practices to ensure smooth collaboration with the core Git team.

## Translations and Localization
Git provides support for translations, also known as localization, of its messages. Contributions to translations are managed via Portable Object (`.po`) files, and those interested in contributing can refer to the `po/README.md` file for details on how to get started.

## Security Vulnerabilities
If you discover any security vulnerabilities or potential threats within the Git project, please report them privately to the Git Security mailing list:

- `git-security@googlegroups.com`

This ensures that vulnerabilities can be addressed quickly and responsibly before being publicly disclosed.

## What's Cooking in Git Development
The Git maintainer regularly sends out "What's Cooking" reports to the mailing list. These reports provide insights into ongoing development efforts, pending patches, and future directions for the project.

## The Origin of the Name "Git"
The name "Git" has an interesting origin, as explained by its creator, Linus Torvalds. Linus jokingly referred to the name as:

- **"Stupid, Contemptible, Despicable"**: A self-deprecating choice by Torvalds.
- **"Global Information Tracker"**: For when everything is working well and the tool tracks information efficiently.
- **"Goddamn Idiotic Truckload of Sh*t"**: For those frustrating moments when things go wrong.

Despite its humorous origins, Git has grown to become a vital tool for developers worldwide.

## Resources for Further Learning
Whether you're new to Git or looking to deepen your understanding, numerous resources are available:

- **Official Website**: [https://git-scm.com/](https://git-scm.com/)  
  Provides full documentation, tutorials, downloads, and community resources.
  
- **GitHub**: [https://github.com/git/git](https://github.com/git/git)  
  The official Git repository, where you can contribute, explore issues, and track the project's development.
  
- **Pro Git Book**: [https://git-scm.com/book/en/v2](https://git-scm.com/book/en/v2)  
  Free and extensive guide to Git.

## Getting Help
If you need help with Git, there are many ways to get assistance:

- **Online Documentation**: [https://git-scm.com/doc](https://git-scm.com/doc)
- **Stack Overflow**: Thousands of questions and answers related to Git usage.
- **Git Mailing List**: Ask the Git community directly via the mailing list.

Remember, Git is a tool that offers immense power and flexibility, but mastering it takes time and practice. Start small, and gradually explore more complex features as you grow comfortable with the basics.

[INSTALL]: INSTALL  
[Documentation/gittutorial.txt]: Documentation/gittutorial.txt  
[Documentation/giteveryday.txt]: Documentation/giteveryday.txt  
[Documentation/gitcvs-migration.txt]: Documentation/gitcvs-migration.txt  
[Documentation/SubmittingPatches]: Documentation/SubmittingPatches  
[Documentation/CodingGuidelines]: Documentation/CodingGuidelines  
[po/README.md]: po/README.md
