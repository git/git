#!/usr/bin/perl -w

print <<'EOF' ;
<a href="http://3.bp.blogspot.com/-zbY2zfS4fKE/TlgfTSTK-oI/AAAAAAAACOQ/E_0Y4408QRE/s1600/GprofileSmall.png" imageanchor="1" style="clear: right; float: right; margin-bottom: 1em; margin-left: 1em;"><img border="0" src="http://3.bp.blogspot.com/-zbY2zfS4fKE/TlgfTSTK-oI/AAAAAAAACOQ/E_0Y4408QRE/s1600/GprofileSmall.png"></a>
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
