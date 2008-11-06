#!/usr/bin/perl -w

print <<'EOF' ;
<html>
<head>
<style>
div.inset {
background: #aff;
color: #888;
margin-left: 10%;
margin-top: 2em;
margin-bottom: 2em;
width: 60%;
padding: 1.2em;
}
div.inset {
color: #444;
}
div.inset a {
color: #444;
}
div.inset a:hover {
color: #00f;
}
h2 {
text-decoration: underline;
color: #888;
}
span.tt {
font-family: monospace;
}
img#ohloh-badge, img#git {
border: none;
float: right;
}
</style>
</head>
<body>
<img src="http://members.cox.net/junkio/Kun-Wave.gif" id="git"
width="64" height="64" />
<h1>A Message from the Git Maintainer</h1>
EOF

sub show_links {
	local ($_) = @_;
	my $br = '';
	for (split(/\n/, $_)) {
		s/^\s*//;
		s/\s*\Z//;
		my $url = $_;
		my $comment = $_;
		$url =~ s/ .*//;
		if ($url =~ /^http:/) {
			print "$br<a href=\"$url\"\n>$comment</a>";
		} else {
			print "$br$comment";
		}
		$br = "<br />\n";
	}
	print "\n";
}

sub show_commands {
	local ($_) = @_;
	my $br = '';
	for (split(/\n/, $_)) {
		s/^\s*//;
		s/\s*\Z//;
		print "$br<span class=\"tt\">$_</span>";
		$br = "<br />\n";
	}
	print "\n";
}

my $in_ul;
$/ = "";
while (<>) {
	$_ =~ s/\n+$//s;

	if (/^ - /) {
		if (!$in_ul) {
			$in_ul = 1;
			print "<ul>\n";
		}
		s/^ - //;
		print "<li>$_</li>\n";
		next;
	}

	if ($in_ul) {
		$in_ul = undef;
		print "</ul>\n\n";
	}

	if (s/^\*\s*//) {
		print "<h2>$_</h2>\n\n";
	} elsif (s/^ {4,}//) {
		print "<div class=\"inset\">\n";
		if (/^(http|git|nntp):\/\//) {
			show_links($_);
		} else {
			show_commands($_);
		}
		print "</div>\n\n";
	} else {
		print "<p>$_</p>\n\n";
	}
}

print <<'EOF' ;
<a href="http://www.ohloh.net/accounts/5439?ref=Detailed">
<img height='35' width='191' id='ohloh-badge'
src='http://www.ohloh.net/accounts/5439/widgets/account_detailed.gif'
alt="ohloh profile for Junio C Hamano" />
</a>
</body>
</html>
EOF
