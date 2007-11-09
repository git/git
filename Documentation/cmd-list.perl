#!/usr/bin/perl -w

use File::Compare qw(compare);

sub format_one {
	my ($out, $nameattr) = @_;
	my ($name, $attr) = @$nameattr;
	my ($state, $description);
	$state = 0;
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
		print $out "gitlink:$name\[1\]::\n\t";
		if ($attr) {
			print $out "($attr) ";
		}
		print $out "$text.\n\n";
	}
	else {
		die "Description does not match $name: $description";
	}
}

my %cmds = ();
while (<DATA>) {
	next if /^#/;

	chomp;
	my ($name, $cat, $attr) = /^(\S+)\s+(.*?)(?:\s+(.*))?$/;
	push @{$cmds{$cat}}, [$name, $attr];
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

	if (-f "$out" && compare("$out", "$out+") == 0) {
		unlink "$out+";
	}
	else {
		print STDERR "$out\n";
		rename "$out+", "$out";
	}
}

# The following list is sorted with "sort -d" to make it easier
# to find entry in the resulting git.html manual page.
__DATA__
git-add                                 mainporcelain
git-am                                  mainporcelain
git-annotate                            ancillaryinterrogators
git-apply                               plumbingmanipulators
git-archimport                          foreignscminterface
git-archive                             mainporcelain
git-bisect                              mainporcelain
git-blame                               ancillaryinterrogators
git-branch                              mainporcelain
git-bundle                              mainporcelain
git-cat-file                            plumbinginterrogators
git-check-attr                          purehelpers
git-checkout                            mainporcelain
git-checkout-index                      plumbingmanipulators
git-check-ref-format                    purehelpers
git-cherry                              ancillaryinterrogators
git-cherry-pick                         mainporcelain
git-citool                              mainporcelain
git-clean                               mainporcelain
git-clone                               mainporcelain
git-commit                              mainporcelain
git-commit-tree                         plumbingmanipulators
git-config                              ancillarymanipulators
git-count-objects                       ancillaryinterrogators
git-cvsexportcommit                     foreignscminterface
git-cvsimport                           foreignscminterface
git-cvsserver                           foreignscminterface
git-daemon                              synchingrepositories
git-describe                            mainporcelain
git-diff                                mainporcelain
git-diff-files                          plumbinginterrogators
git-diff-index                          plumbinginterrogators
git-diff-tree                           plumbinginterrogators
git-fast-import				ancillarymanipulators
git-fetch                               mainporcelain
git-fetch-pack                          synchingrepositories
git-filter-branch                       ancillarymanipulators
git-fmt-merge-msg                       purehelpers
git-for-each-ref                        plumbinginterrogators
git-format-patch                        mainporcelain
git-fsck	                        ancillaryinterrogators
git-gc                                  mainporcelain
git-get-tar-commit-id                   ancillaryinterrogators
git-grep                                mainporcelain
git-gui                                 mainporcelain
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
git-lost-found                          ancillarymanipulators	deprecated
git-ls-files                            plumbinginterrogators
git-ls-remote                           plumbinginterrogators
git-ls-tree                             plumbinginterrogators
git-mailinfo                            purehelpers
git-mailsplit                           purehelpers
git-merge                               mainporcelain
git-merge-base                          plumbinginterrogators
git-merge-file                          plumbingmanipulators
git-merge-index                         plumbingmanipulators
git-merge-one-file                      purehelpers
git-mergetool                           ancillarymanipulators
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
git-remote                              ancillarymanipulators
git-repack                              ancillarymanipulators
git-request-pull                        foreignscminterface
git-rerere                              ancillaryinterrogators
git-reset                               mainporcelain
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
git-stash                               mainporcelain
git-status                              mainporcelain
git-stripspace                          purehelpers
git-submodule                           mainporcelain
git-svn                                 foreignscminterface
git-symbolic-ref                        plumbingmanipulators
git-tag                                 mainporcelain
git-tar-tree                            plumbinginterrogators	deprecated
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
