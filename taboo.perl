#!/usr/bin/perl -w

my $tmpl = '	if (%%PATTERN%%) {
		print "$lineno ${_}matches %%QPATTERN%%\n";
		return;
	}
';
my $stmt = "";
my $in_header = 1;

while (<DATA>) {
	if (/^\$global_taboo_body =/) {
		$in_header = 0;
	}
	next if (/^\043/ || /^\$/ || /^END$/ || /^\s*$/);
	chomp;
	my $p = $_;
	if ($in_header) {
		$p = '/^[-\w_]*:/ && ' . $p;
	}
	my $q = quotemeta($p);
	my $stmt1 = $tmpl;
	$stmt1 =~ s|%%PATTERN%%|$p|g;
	$stmt1 =~ s|%%QPATTERN%%|$q|g;
	$stmt .= $stmt1;
}
close DATA;

$stmt = 'sub check {
	my ($line, $lineno) = @_;
' . $stmt . '
}
';
eval $stmt;
while (<>) {
	check($_, $.);
}

my $how_to_update_this_script = <<'EOF' ;
	( sed -e '/^__DATA__$/q' taboo.perl && \
	  wget -q -O - http://vger.kernel.org/majordomo-taboos.txt ) \
		>taboo.perl+
	if diff -u taboo.perl taboo.perl+; \
	then \
		rm -f taboo.perl+; \
		echo >&2 No changes.; \
	else \
		mv taboo.perl+ taboo.perl; \
		chmod +x taboo.perl; \
	fi
EOF

__DATA__
#TABOO-START
#
# These are Majordomo's  global  majordomo.cf  as used at
# vger.kernel.org.  This is automated extract from running
# system configuration. THESE MIGHT NOT BE USEFULL IN ANY
# OTHER ENVIRONMENT, AND THIS EXTRACT IS SHOWN ONLY FOR
# YOU TO SEE, WHAT TRIGGERS BLOCKING AT VGER'S LISTS.
#
# taboo headers to catch
#
$global_taboo_headers = <<'END';
m/From:.*MAILER-DAEMON/i
m/^Mailing-List:/i
m/^list-/i
/^subject: ndn: /i
/^subject:\s*RCPT:/i
/^subject:\s*Delivery Confirmation\b/i
/^subject:\s*NON-DELIVERY of:/i
/^subject:.*Undeliverable Message\b/i
/^subject:.*Receipt Confirmation\b/i
/^subject:.*Failed mail\b/i
/^subject:.*Returned mail\b/i
/^subject:\s*unable to deliver mail\b/i
/^subject:\s.*\baway from my mail\b/i
/^subject:\s*Autoreply/i
/^subject:\s*Path Too Long fixer/i
/^subject:\s*Buy In-Stream preroll video/i
/#field0#/
m%content-type:.*text/html%i
/x-mailing-list:.*\@vger\.kernel\.org/i
# DATE:   25 Jun 01 3:08:39 AM
m/DATE:\s*..\s...\s..\s.*:..:..\s..\s*$/i
m/nntp-server.caltech.edu/
m/Mail Bomber/
m/X-Mailman-Version:/
m/X-EM-Registration:/
m/x-esmtp:/
m/Local time zone must be set/
m/X-Mailer:.*eMerge/i
m/X-Mailer:.Trade-Navigator/i
m/From:.*MAILER-DAEMON/i
m/X-Mailer:.*Group Mail/
m/^Status:/
m/^X-Status:/
m/X-Set:/
m/^X-Mailer:.*JiXing/
m/^X-Mailer:.*MailXSender/
m!Message-Id:.*<.*\@vger.kernel.org>!
m!Message-Id:.*<.*\@zeus.kernel.org>!
m/Subject:.*detected a virus /
m/Subject:.*Acai/
#m/Anti-Virus/i
m/Subject:.*[Vv]irus [Ff]ound/
m/Subject:.*[Vv]irus [Aa]lert/
m/^Subject:\s*Report to Sender/
m/^Subject:.*AntiVir ALARM/
m!^X-Library:\s*Indy!
m!Content-Type:\s*application/x-msdownload!
m!Conetnt-Type:\s*application/msword!
m!MiME-!
m!netdev-bounce\@oss\.sgi\.com!
m!Undeliverable:!
m!Syntax error in!
m!^Illegal-Object:!
m!Subject:.*paycheck!i
m!Subject:.*Urgent\s*Business\s*Request!i
m!Subject:.*Urgent\s*Business!i
m!Subject:.*Business\s*Request!i
m!Subject:.*tiffany\s*uk!i
m!Subject:.*pandra\s*charms!i
m!Subject:.*Mail delivery failure!
m!Subject.*\[SPAM\]!
m!Subject:.*Norton AntiVirus detected!
m!X-Spam-Flag:.*YES!
m!Subject:.*\sSARS\s!i
m!Subject:.*MMS Notification!
m!Subject:.*Rejected Mail!
m!Subject:.*Report to Recipient!
m!Subject:.*You sent potentially!
m!WAVA Postmaster!
m!^SUBJECT:!
m!Delivered-To:!
m!^From:\s*Majordomo!
m!Subject:\s+Out of Office AutoReply!
#m!Content-Type: multipart/alternative!
m!From:.*amavisd-new!
m!Subject:.*found.*virus!i
m!Subject:.*As Seen on CNN!i
m!Subject:.*Mail Delivery!
m!Subject:.*Essential.*Software.*On.*CD!i
m!ScanMail for Lotus Notes!
m!InternetBank Agreement!
m!X-WEBC-Mail-From-Script:!
m!X-Mailer: RLSP Mailer!
m!Subject: Rediff\'s Auto Response!
m!Email account utilization warning!
m!From:.*Lyris.List!
m!Listar command results!
m!EHLO vger.kernel.org!
m!HELO vger.kernel.org!
m!stk-sub!
m!owner-majordomo\@!
m!LOTTERY!i
m!SWEEPSTAKE!i
m!GSM wireless terminal from China!
m!http://vk.com/!
m!eyari\.com!i
END

# TABOO BODY
#
# Taboo body contents to catch and forward to the approval address
#
# For example:
#   $global_taboo_body = <<'END';
#   /taboo topic/i
#   /another taboo/i
#   END
# NOTE! Using ' instead of " in the next line is VERY IMPORTANT!!!
#
$global_taboo_body = <<'END';
m!MailEnable: You are not permitted to post to the list!o
m!^X-Mailing-List: !o
m!^List-ID: !o
m%Content-Type:.*text/html%io
m%Content-Type:.*multipart/alternative%io
#m/charset=.*windows-/io
m!Webmail Administrator!o
m!Re-type Password!o
m!FROM THE DESK OF!io
m!Your mailbox quota!io
m!Dear lucky winner!io
m!Welcome to our Newsletter!o
END

#TABOO-END
