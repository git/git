#!tcsh
#
# tcsh completion support for core Git.
#
# Copyright (C) 2012 Marc Khouzam <marc.khouzam@gmail.com>
# Distributed under the GNU General Public License, version 2.0.
#
# When sourced, this script will generate a new script that uses
# the git-completion.bash script provided by core Git.  This new
# script can be used by tcsh to perform git completion.
# The current script also issues the necessary tcsh 'complete'
# commands.
#
# To use this completion script:
#
#    1) Copy both this file and the bash completion script to ${HOME}.
#       You _must_ use the name ${HOME}/.git-completion.bash for the
#       bash script.
#       (e.g. ~/.git-completion.tcsh and ~/.git-completion.bash).
#    2) Add the following line to your .tcshrc/.cshrc:
#        source ~/.git-completion.tcsh

set __git_tcsh_completion_original_script = ${HOME}/.git-completion.bash
set __git_tcsh_completion_script = ${HOME}/.git-completion.tcsh.bash

# Check that the user put the script in the right place
if ( ! -e ${__git_tcsh_completion_original_script} ) then
       echo "git-completion.tcsh: Cannot find: ${__git_tcsh_completion_original_script}.  Git completion will not work."
       exit
endif

cat << EOF > ${__git_tcsh_completion_script}
#!bash
#
# This script is GENERATED and will be overwritten automatically.
# Do not modify it directly.  Instead, modify the git-completion.tcsh
# script provided by Git core.
#

source ${__git_tcsh_completion_original_script}

# Set COMP_WORDS in a way that can be handled by the bash script.
COMP_WORDS=(\$2)

# The cursor is at the end of parameter #1.
# We must check for a space as the last character which will
# tell us that the previous word is complete and the cursor
# is on the next word.
if [ "\${2: -1}" == " " ]; then
       # The last character is a space, so our location is at the end
       # of the command-line array
       COMP_CWORD=\${#COMP_WORDS[@]}
else
       # The last character is not a space, so our location is on the
       # last word of the command-line array, so we must decrement the
       # count by 1
       COMP_CWORD=\$((\${#COMP_WORDS[@]}-1))
fi

# Call _git() or _gitk() of the bash script, based on the first argument
_\${1}

IFS=\$'\n'
echo "\${COMPREPLY[*]}" | sort | uniq
EOF

complete git  'p/*/`bash ${__git_tcsh_completion_script} git "${COMMAND_LINE}"`/'
complete gitk 'p/*/`bash ${__git_tcsh_completion_script} gitk "${COMMAND_LINE}"`/'
