#!/usr/bin/perl -w -s
# Copyright (C) 2012
#     Charles Roussel <charles.roussel@ensimag.imag.fr>
#     Simon Cathebras <simon.cathebras@ensimag.imag.fr>
#     Julien Khayat <julien.khayat@ensimag.imag.fr>
#     Guillaume Sasdy <guillaume.sasdy@ensimag.imag.fr>
#     Simon Perrat <simon.perrat@ensimag.imag.fr>
# License: GPL v2 or later

# Usage:
#       ./test-gitmw.pl <command> [argument]*
# Execute in terminal using the name of the function to call as first
# parameter, and the function's arguments as following parameters
#
# Example:
#     ./test-gitmw.pl "get_page" foo .
# will call <wiki_getpage> with arguments <foo> and <.>
#
# Available functions are:
#     "get_page"
#     "delete_page"
#     "edit_page"
#     "getallpagename"

use MediaWiki::API;
use Getopt::Long;
use encoding 'utf8';
use DateTime::Format::ISO8601;
use open ':encoding(utf8)';
use constant SLASH_REPLACEMENT => "%2F";

#Parsing of the config file

my $configfile = "$ENV{'CURR_DIR'}/test.config";
my %config;
open my $CONFIG, "<",  $configfile or die "can't open $configfile: $!";
while (<$CONFIG>)
{
	chomp;
	s/#.*//;
	s/^\s+//;
	s/\s+$//;
	next unless length;
	my ($key, $value) = split (/\s*=\s*/,$_, 2);
	$config{$key} = $value;
	last if ($key eq 'LIGHTTPD' and $value eq 'false');
	last if ($key eq 'PORT');
}
close $CONFIG or die "can't close $configfile: $!";

my $wiki_address = "http://$config{'SERVER_ADDR'}".":"."$config{'PORT'}";
my $wiki_url = "$wiki_address/$config{'WIKI_DIR_NAME'}/api.php";
my $wiki_admin = "$config{'WIKI_ADMIN'}";
my $wiki_admin_pass = "$config{'WIKI_PASSW'}";
my $mw = MediaWiki::API->new;
$mw->{config}->{api_url} = $wiki_url;


# wiki_login <name> <password>
#
# Logs the user with <name> and <password> in the global variable
# of the mediawiki $mw
sub wiki_login {
	$mw->login( { lgname => "$_[0]",lgpassword => "$_[1]" } )
	|| die "getpage: login failed";
}

# wiki_getpage <wiki_page> <dest_path>
#
# fetch a page <wiki_page> from the wiki referenced in the global variable
# $mw and copies its content in directory dest_path
sub wiki_getpage {
	my $pagename = $_[0];
	my $destdir = $_[1];

	my $page = $mw->get_page( { title => $pagename } );
	if (!defined($page)) {
		die "getpage: wiki does not exist";
	}

	my $content = $page->{'*'};
	if (!defined($content)) {
		die "getpage: page does not exist";
	}

	$pagename=$page->{'title'};
	# Replace spaces by underscore in the page name
	$pagename =~ s/ /_/g;
	$pagename =~ s/\//%2F/g;
	open(my $file, ">$destdir/$pagename.mw");
	print $file "$content";
	close ($file);

}

# wiki_delete_page <page_name>
#
# delete the page with name <page_name> from the wiki referenced
# in the global variable $mw
sub wiki_delete_page {
	my $pagename = $_[0];

	my $exist=$mw->get_page({title => $pagename});

	if (defined($exist->{'*'})){
		$mw->edit({ action => 'delete',
				title => $pagename})
		|| die $mw->{error}->{code} . ": " . $mw->{error}->{details};
	} else {
		die "no page with such name found: $pagename\n";
	}
}

# wiki_editpage <wiki_page> <wiki_content> <wiki_append> [-c=<category>] [-s=<summary>]
#
# Edit a page named <wiki_page> with content <wiki_content> on the wiki
# referenced with the global variable $mw
# If <wiki_append> == true : append <wiki_content> at the end of the actual
# content of the page <wiki_page>
# If <wik_page> doesn't exist, that page is created with the <wiki_content>
sub wiki_editpage {
	my $wiki_page = $_[0];
	my $wiki_content = $_[1];
	my $wiki_append = $_[2];
	my $summary = "";
	my ($summ, $cat) = ();
	GetOptions('s=s' => \$summ, 'c=s' => \$cat);

	my $append = 0;
	if (defined($wiki_append) && $wiki_append eq 'true') {
		$append=1;
	}

	my $previous_text ="";

	if ($append) {
		my $ref = $mw->get_page( { title => $wiki_page } );
		$previous_text = $ref->{'*'};
	}

	my $text = $wiki_content;
	if (defined($previous_text)) {
		$text="$previous_text$text";
	}

	# Eventually, add this page to a category.
	if (defined($cat)) {
		my $category_name="[[Category:$cat]]";
		$text="$text\n $category_name";
	}
	if(defined($summ)){
		$summary=$summ;
	}

	$mw->edit( { action => 'edit', title => $wiki_page, summary => $summary, text => "$text"} );
}

# wiki_getallpagename [<category>]
#
# Fetch all pages of the wiki referenced by the global variable $mw
# and print the names of each one in the file all.txt with a new line
# ("\n") between these.
# If the argument <category> is defined, then this function get only the pages
# belonging to <category>.
sub wiki_getallpagename {
	# fetch the pages of the wiki
	if (defined($_[0])) {
		my $mw_pages = $mw->list ( { action => 'query',
				list => 'categorymembers',
				cmtitle => "Category:$_[0]",
				cmnamespace => 0,
				cmlimit => 500 },
		)
		|| die $mw->{error}->{code}.": ".$mw->{error}->{details};
		open(my $file, ">all.txt");
		foreach my $page (@{$mw_pages}) {
			print $file "$page->{title}\n";
		}
		close ($file);

	} else {
		my $mw_pages = $mw->list({
				action => 'query',
				list => 'allpages',
				aplimit => 500,
			})
		|| die $mw->{error}->{code}.": ".$mw->{error}->{details};
		open(my $file, ">all.txt");
		foreach my $page (@{$mw_pages}) {
			print $file "$page->{title}\n";
		}
		close ($file);
	}
}

sub wiki_upload_file {
	my $file_name = $_[0];
	my $resultat = $mw->edit ( {
		action => 'upload',
		filename => $file_name,
		comment => 'upload a file',
		file => [ $file_name ],
		ignorewarnings=>1,
	}, {
		skip_encoding => 1
	} ) || die $mw->{error}->{code} . ' : ' . $mw->{error}->{details};
}



# Main part of this script: parse the command line arguments
# and select which function to execute
my $fct_to_call = shift;

wiki_login($wiki_admin, $wiki_admin_pass);

my %functions_to_call = qw(
	upload_file    wiki_upload_file
	get_page       wiki_getpage
	delete_page    wiki_delete_page
	edit_page      wiki_editpage
	getallpagename wiki_getallpagename
);
die "$0 ERROR: wrong argument" unless exists $functions_to_call{$fct_to_call};
&{$functions_to_call{$fct_to_call}}(@ARGV);
