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
		synchingrepositories
		foreignscminterface
		purehelpers
		synchelpers)) {
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
git-applymbox                           ancillaryinterrogators
git-applypatch                          purehelpers
git-apply                               plumbingmanipulators
git-archimport                          foreignscminterface
git-archive                             mainporcelain
git-bisect                              mainporcelain
git-blame                               ancillaryinterrogators
git-branch                              mainporcelain
git-cat-file                            plumbinginterrogators
git-checkout-index                      plumbingmanipulators
git-checkout                            mainporcelain
git-check-ref-format                    purehelpers
git-cherry                              ancillaryinterrogators
git-cherry-pick                         mainporcelain
git-clean                               mainporcelain
git-clone                               mainporcelain
git-commit                              mainporcelain
git-commit-tree                         plumbingmanipulators
git-convert-objects                     ancillarymanipulators
git-count-objects                       ancillaryinterrogators
git-cvsexportcommit                     foreignscminterface
git-cvsimport                           foreignscminterface
git-cvsserver                           foreignscminterface
git-daemon                              synchingrepositories
git-describe                            mainporcelain
git-diff-files                          plumbinginterrogators
git-diff-index                          plumbinginterrogators
git-diff                                mainporcelain
git-diff-stages                         plumbinginterrogators
git-diff-tree                           plumbinginterrogators
git-fetch                               mainporcelain
git-fetch-pack                          synchingrepositories
git-fmt-merge-msg                       purehelpers
git-for-each-ref                        plumbinginterrogators
git-format-patch                        mainporcelain
git-fsck	                        ancillaryinterrogators
git-gc                                  mainporcelain
git-get-tar-commit-id                   ancillaryinterrogators
git-grep                                mainporcelain
git-hash-object                         plumbingmanipulators
git-http-fetch                          synchelpers
git-http-push                           synchelpers
git-imap-send                           foreignscminterface
git-index-pack                          plumbingmanipulators
git-init                                mainporcelain
git-instaweb                            ancillaryinterrogators
gitk                                    mainporcelain
git-local-fetch                         synchingrepositories
git-log                                 mainporcelain
git-lost-found                          ancillarymanipulators
git-ls-files                            plumbinginterrogators
git-ls-remote                           plumbinginterrogators
git-ls-tree                             plumbinginterrogators
git-mailinfo                            purehelpers
git-mailsplit                           purehelpers
git-merge-base                          plumbinginterrogators
git-merge-file                          plumbingmanipulators
git-merge-index                         plumbingmanipulators
git-merge                               mainporcelain
git-merge-one-file                      purehelpers
git-merge-tree                          ancillaryinterrogators
git-mktag                               plumbingmanipulators
git-mktree                              plumbingmanipulators
git-mv                                  mainporcelain
git-name-rev                            plumbinginterrogators
git-pack-objects                        plumbingmanipulators
git-pack-redundant                      plumbinginterrogators
git-pack-refs                           ancillarymanipulators
git-parse-remote                        synchelpers
git-patch-id                            purehelpers
git-peek-remote                         purehelpers
git-prune                               ancillarymanipulators
git-prune-packed                        plumbingmanipulators
git-pull                                mainporcelain
git-push                                mainporcelain
git-quiltimport                         foreignscminterface
git-read-tree                           plumbingmanipulators
git-rebase                              mainporcelain
git-receive-pack                        synchelpers
git-reflog                              ancillarymanipulators
git-relink                              ancillarymanipulators
git-repack                              ancillarymanipulators
git-config                              ancillarymanipulators
git-request-pull                        foreignscminterface
git-rerere                              ancillaryinterrogators
git-reset                               mainporcelain
git-resolve                             mainporcelain
git-revert                              mainporcelain
git-rev-list                            plumbinginterrogators
git-rev-parse                           ancillaryinterrogators
git-rm                                  mainporcelain
git-runstatus                           ancillaryinterrogators
git-send-email                          foreignscminterface
git-send-pack                           synchingrepositories
git-shell                               synchelpers
git-shortlog                            mainporcelain
git-show                                mainporcelain
git-show-branch                         ancillaryinterrogators
git-show-index                          plumbinginterrogators
git-show-ref                            plumbinginterrogators
git-sh-setup                            purehelpers
git-ssh-fetch                           synchingrepositories
git-ssh-upload                          synchingrepositories
git-status                              mainporcelain
git-stripspace                          purehelpers
git-svn                                 foreignscminterface
git-svnimport                           foreignscminterface
git-symbolic-ref                        plumbingmanipulators
git-tag                                 mainporcelain
git-tar-tree                            plumbinginterrogators
git-unpack-file                         plumbinginterrogators
git-unpack-objects                      plumbingmanipulators
git-update-index                        plumbingmanipulators
git-update-ref                          plumbingmanipulators
git-update-server-info                  synchingrepositories
git-upload-archive                      synchelpers
git-upload-pack                         synchelpers
git-var                                 plumbinginterrogators
git-verify-pack                         plumbinginterrogators
git-verify-tag                          ancillaryinterrogators
git-whatchanged                         ancillaryinterrogators
git-write-tree                          plumbingmanipulators
