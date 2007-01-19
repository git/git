#

sub format_one {
	my ($out, $name) = @_;
	my ($state, $description);
	open I, '<', "$name.txt" or die "No such file $name.txt";
	while (<I>) {
		if (/^NAME$/) {
			$state = 1;
			next;
		}
		if ($state == 1 && /^----$/) {
			$state = 2;
			next;
		}
		next if ($state != 2);
		chomp;
		$description = $_;
		last;
	}
	close I;
	if (!defined $description) {
		die "No description found in $name.txt";
	}
	if (my ($verify_name, $text) = ($description =~ /^($name) - (.*)/)) {
		print $out "gitlink:$name\[1\]::\n";
		print $out "\t$text.\n\n";
	}
	else {
		die "Description does not match $name: $description";
	}
}

my %cmds = ();
while (<DATA>) {
	next if /^#/;

	chomp;
	my ($name, $cat) = /^(\S+)\s+(.*)$/;
	push @{$cmds{$cat}}, $name;
}

for my $cat (qw(ancillaryinterrogators
		ancillarymanipulators
		mainporcelain
		plumbinginterrogators
		plumbingmanipulators
		synchingrepositories)) {
	my $out = "cmds-$cat.txt";
	open O, '>', "$out+" or die "Cannot open output file $out+";
	for (@{$cmds{$cat}}) {
		format_one(\*O, $_);
	}
	close O;
	rename "$out+", "$out";
}

__DATA__
git-add                                 mainporcelain
git-am                                  mainporcelain
git-annotate                            ancillaryinterrogators
git-applymbox                           mainporcelain
git-applypatch                          ancillarymanipulators
git-apply                               plumbingmanipulators
git-archimport                          ancillarymanipulators
git-archive                             mainporcelain
git-bisect                              mainporcelain
git-blame                               ancillaryinterrogators
git-branch                              mainporcelain
git-cat-file                            plumbinginterrogators
git-checkout-index                      plumbingmanipulators
git-checkout                            mainporcelain
git-check-ref-format                    ancillaryinterrogators
git-cherry                              ancillaryinterrogators
git-cherry-pick                         mainporcelain
git-clean                               mainporcelain
git-clone                               mainporcelain
git-commit                              mainporcelain
git-commit-tree                         plumbingmanipulators
git-convert-objects                     ancillarymanipulators
git-count-objects                       ancillaryinterrogators
git-cvsexportcommit                     ancillarymanipulators
git-cvsimport                           ancillarymanipulators
git-cvsserver                           ancillarymanipulators
git-daemon                              ancillaryinterrogators
git-describe                            plumbinginterrogators
git-diff-files                          plumbinginterrogators
git-diff-index                          plumbinginterrogators
git-diff                                mainporcelain
git-diff-stages                         plumbinginterrogators
git-diff-tree                           plumbinginterrogators
git-fetch                               mainporcelain
git-fetch-pack                          synchingrepositories
git-fmt-merge-msg                       ancillaryinterrogators
git-for-each-ref                        plumbinginterrogators
git-format-patch                        mainporcelain
git-fsck-objects                        plumbinginterrogators
git-gc                                  ancillarymanipulators
git-get-tar-commit-id                   ancillaryinterrogators
git-grep                                mainporcelain
git-hash-object                         plumbingmanipulators
git-http-fetch                          synchingrepositories
git-http-push                           synchingrepositories
git-imap-send                           ancillaryinterrogators
git-index-pack                          plumbingmanipulators
git-init                                plumbingmanipulators
git-instaweb                            ancillaryinterrogators
gitk                                    mainporcelain
git-local-fetch                         synchingrepositories
git-log                                 mainporcelain
git-lost-found                          ancillarymanipulators
git-ls-files                            plumbinginterrogators
git-ls-remote                           mainporcelain
git-ls-tree                             plumbinginterrogators
git-mailinfo                            ancillaryinterrogators
git-mailsplit                           ancillaryinterrogators
git-merge-base                          plumbinginterrogators
git-merge-file                          plumbingmanipulators
git-merge-index                         plumbingmanipulators
git-merge                               mainporcelain
git-merge-one-file                      ancillarymanipulators
git-merge-tree                          ancillaryinterrogators
git-mktag                               plumbingmanipulators
git-mktree                              plumbingmanipulators
git-mv                                  mainporcelain
git-name-rev                            plumbinginterrogators
git-pack-objects                        plumbingmanipulators
git-pack-redundant                      plumbinginterrogators
git-pack-refs                           mainporcelain
git-parse-remote                        ancillaryinterrogators
git-patch-id                            ancillaryinterrogators
git-peek-remote                         synchingrepositories
git-prune                               ancillarymanipulators
git-prune-packed                        plumbingmanipulators
git-pull                                mainporcelain
git-push                                mainporcelain
git-quiltimport                         ancillarymanipulators
git-read-tree                           plumbingmanipulators
git-rebase                              mainporcelain
git-receive-pack                        synchingrepositories
git-reflog                              ancillarymanipulators
git-relink                              ancillarymanipulators
git-repack                              mainporcelain
git-repo-config                         plumbingmanipulators
git-request-pull                        ancillaryinterrogators
git-rerere                              mainporcelain
git-reset                               mainporcelain
git-resolve                             mainporcelain
git-revert                              mainporcelain
git-rev-list                            plumbinginterrogators
git-rev-parse                           ancillaryinterrogators
git-rm                                  mainporcelain
git-runstatus                           ancillaryinterrogators
git-send-email                          ancillaryinterrogators
git-send-pack                           synchingrepositories
git-shell                               synchingrepositories
git-shortlog                            mainporcelain
git-show                                mainporcelain
git-show-branch                         mainporcelain
git-show-index                          plumbinginterrogators
git-show-ref                            plumbinginterrogators
git-sh-setup                            ancillarymanipulators
git-ssh-fetch                           synchingrepositories
git-ssh-upload                          synchingrepositories
git-status                              mainporcelain
git-stripspace                          ancillaryinterrogators
git-svn                                 ancillarymanipulators
git-svnimport                           ancillarymanipulators
git-symbolic-ref                        ancillaryinterrogators
git-symbolic-ref                        ancillarymanipulators
git-tag                                 ancillarymanipulators
git-tar-tree                            plumbinginterrogators
git-unpack-file                         plumbinginterrogators
git-unpack-objects                      plumbingmanipulators
git-update-index                        plumbingmanipulators
git-update-ref                          ancillarymanipulators
git-update-server-info                  synchingrepositories
git-upload-archive                      synchingrepositories
git-upload-pack                         synchingrepositories
git-var                                 plumbinginterrogators
git-verify-pack                         plumbinginterrogators
git-verify-tag                          mainporcelain
git-whatchanged                         mainporcelain
git-write-tree                          plumbingmanipulators
