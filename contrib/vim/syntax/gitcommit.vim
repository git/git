syn region gitLine start=/^#/ end=/$/
syn region gitCommit start=/^# Added but not yet committed:$/ end=/^#$/ contains=gitHead,gitCommitFile
syn region gitHead contained start=/^#   (.*)/ end=/^#$/
syn region gitChanged start=/^# Changed but not added:/ end=/^#$/ contains=gitHead,gitChangedFile
syn region gitUntracked start=/^# Untracked files:/ end=/^#$/ contains=gitHead,gitUntrackedFile

syn match gitCommitFile contained /^#\t.*/hs=s+2
syn match gitChangedFile contained /^#\t.*/hs=s+2
syn match gitUntrackedFile contained /^#\t.*/hs=s+2

hi def link gitLine Comment
hi def link gitCommit Comment
hi def link gitChanged Comment
hi def link gitHead Comment
hi def link gitUntracked Comment
hi def link gitCommitFile Type
hi def link gitChangedFile Constant
hi def link gitUntrackedFile Constant
