#!/usr/bin/env perl
#
# Copyright (c) 2021-2022 Eric Sunshine <sunshine@sunshineco.com>
#
# This tool scans shell scripts for test definitions and checks those tests for
# problems, such as broken &&-chains, which might hide bugs in the tests
# themselves or in behaviors being exercised by the tests.
#
# Input arguments are pathnames of shell scripts containing test definitions,
# or globs referencing a collection of scripts. For each problem discovered,
# the pathname of the script containing the test is printed along with the test
# name and the test body with a `?!FOO?!` annotation at the location of each
# detected problem, where "FOO" is a tag such as "AMP" which indicates a broken
# &&-chain. Returns zero if no problems are discovered, otherwise non-zero.

use warnings;
use strict;
use Config;
use File::Glob;
use Getopt::Long;

my $jobs = -1;
my $show_stats;
my $emit_all;

# Lexer tokenizes POSIX shell scripts. It is roughly modeled after section 2.3
# "Token Recognition" of POSIX chapter 2 "Shell Command Language". Although
# similar to lexical analyzers for other languages, this one differs in a few
# substantial ways due to quirks of the shell command language.
#
# For instance, in many languages, newline is just whitespace like space or
# TAB, but in shell a newline is a command separator, thus a distinct lexical
# token. A newline is significant and returned as a distinct token even at the
# end of a shell comment.
#
# In other languages, `1+2` would typically be scanned as three tokens
# (`1`, `+`, and `2`), but in shell it is a single token. However, the similar
# `1 + 2`, which embeds whitepace, is scanned as three token in shell, as well.
# In shell, several characters with special meaning lose that meaning when not
# surrounded by whitespace. For instance, the negation operator `!` is special
# when standing alone surrounded by whitespace; whereas in `foo!uucp` it is
# just a plain character in the longer token "foo!uucp". In many other
# languages, `"string"/foo:'string'` might be scanned as five tokens ("string",
# `/`, `foo`, `:`, and 'string'), but in shell, it is just a single token.
#
# The lexical analyzer for the shell command language is also somewhat unusual
# in that it recursively invokes the parser to handle the body of `$(...)`
# expressions which can contain arbitrary shell code. Such expressions may be
# encountered both inside and outside of double-quoted strings.
#
# The lexical analyzer is responsible for consuming shell here-doc bodies which
# extend from the line following a `<<TAG` operator until a line consisting
# solely of `TAG`. Here-doc consumption begins when a newline is encountered.
# It is legal for multiple here-doc `<<TAG` operators to be present on a single
# line, in which case their bodies must be present one following the next, and
# are consumed in the (left-to-right) order the `<<TAG` operators appear on the
# line. A special complication is that the bodies of all here-docs must be
# consumed when the newline is encountered even if the parse context depth has
# changed. For instance, in `cat <<A && x=$(cat <<B &&\n`, bodies of here-docs
# "A" and "B" must be consumed even though "A" was introduced outside the
# recursive parse context in which "B" was introduced and in which the newline
# is encountered.
package Lexer;

sub new {
	my ($class, $parser, $s) = @_;
	bless {
		parser => $parser,
		buff => $s,
		lineno => 1,
		heretags => []
	} => $class;
}

sub scan_heredoc_tag {
	my $self = shift @_;
	${$self->{buff}} =~ /\G(-?)/gc;
	my $indented = $1;
	my $token = $self->scan_token();
	return "<<$indented" unless $token;
	my $tag = $token->[0];
	$tag =~ s/['"\\]//g;
	$$token[0] = $indented ? "\t$tag" : "$tag";
	push(@{$self->{heretags}}, $token);
	return "<<$indented$tag";
}

sub scan_op {
	my ($self, $c) = @_;
	my $b = $self->{buff};
	return $c unless $$b =~ /\G(.)/sgc;
	my $cc = $c . $1;
	return scan_heredoc_tag($self) if $cc eq '<<';
	return $cc if $cc =~ /^(?:&&|\|\||>>|;;|<&|>&|<>|>\|)$/;
	pos($$b)--;
	return $c;
}

sub scan_sqstring {
	my $self = shift @_;
	${$self->{buff}} =~ /\G([^']*'|.*\z)/sgc;
	my $s = $1;
	$self->{lineno} += () = $s =~ /\n/sg;
	return "'" . $s;
}

sub scan_dqstring {
	my $self = shift @_;
	my $b = $self->{buff};
	my $s = '"';
	while (1) {
		# slurp up non-special characters
		$s .= $1 if $$b =~ /\G([^"\$\\]+)/gc;
		# handle special characters
		last unless $$b =~ /\G(.)/sgc;
		my $c = $1;
		$s .= '"', last if $c eq '"';
		$s .= '$' . $self->scan_dollar(), next if $c eq '$';
		if ($c eq '\\') {
			$s .= '\\', last unless $$b =~ /\G(.)/sgc;
			$c = $1;
			$self->{lineno}++, next if $c eq "\n"; # line splice
			# backslash escapes only $, `, ", \ in dq-string
			$s .= '\\' unless $c =~ /^[\$`"\\]$/;
			$s .= $c;
			next;
		}
		die("internal error scanning dq-string '$c'\n");
	}
	$self->{lineno} += () = $s =~ /\n/sg;
	return $s;
}

sub scan_balanced {
	my ($self, $c1, $c2) = @_;
	my $b = $self->{buff};
	my $depth = 1;
	my $s = $c1;
	while ($$b =~ /\G([^\Q$c1$c2\E]*(?:[\Q$c1$c2\E]|\z))/gc) {
		$s .= $1;
		$depth++, next if $s =~ /\Q$c1\E$/;
		$depth--;
		last if $depth == 0;
	}
	$self->{lineno} += () = $s =~ /\n/sg;
	return $s;
}

sub scan_subst {
	my $self = shift @_;
	my @tokens = $self->{parser}->parse(qr/^\)$/);
	$self->{parser}->next_token(); # closing ")"
	return @tokens;
}

sub scan_dollar {
	my $self = shift @_;
	my $b = $self->{buff};
	return $self->scan_balanced('(', ')') if $$b =~ /\G\((?=\()/gc; # $((...))
	return '(' . join(' ', map {$_->[0]} $self->scan_subst()) . ')' if $$b =~ /\G\(/gc; # $(...)
	return $self->scan_balanced('{', '}') if $$b =~ /\G\{/gc; # ${...}
	return $1 if $$b =~ /\G(\w+)/gc; # $var
	return $1 if $$b =~ /\G([@*#?$!0-9-])/gc; # $*, $1, $$, etc.
	return '';
}

sub swallow_heredocs {
	my $self = shift @_;
	my $b = $self->{buff};
	my $tags = $self->{heretags};
	while (my $tag = shift @$tags) {
		my $start = pos($$b);
		my $indent = $$tag[0] =~ s/^\t// ? '\\s*' : '';
		$$b =~ /(?:\G|\n)$indent\Q$$tag[0]\E(?:\n|\z)/gc;
		if (pos($$b) > $start) {
			my $body = substr($$b, $start, pos($$b) - $start);
			$self->{lineno} += () = $body =~ /\n/sg;
			next;
		}
		push(@{$self->{parser}->{problems}}, ['UNCLOSED-HEREDOC', $tag]);
		$$b =~ /(?:\G|\n).*\z/gc; # consume rest of input
		my $body = substr($$b, $start, pos($$b) - $start);
		$self->{lineno} += () = $body =~ /\n/sg;
		last;
	}
}

sub scan_token {
	my $self = shift @_;
	my $b = $self->{buff};
	my $token = '';
	my ($start, $startln);
RESTART:
	$startln = $self->{lineno};
	$$b =~ /\G[ \t]+/gc; # skip whitespace (but not newline)
	$start = pos($$b) || 0;
	$self->{lineno}++, return ["\n", $start, pos($$b), $startln, $startln] if $$b =~ /\G#[^\n]*(?:\n|\z)/gc; # comment
	while (1) {
		# slurp up non-special characters
		$token .= $1 if $$b =~ /\G([^\\;&|<>(){}'"\$\s]+)/gc;
		# handle special characters
		last unless $$b =~ /\G(.)/sgc;
		my $c = $1;
		pos($$b)--, last if $c =~ /^[ \t]$/; # whitespace ends token
		pos($$b)--, last if length($token) && $c =~ /^[;&|<>(){}\n]$/;
		$token .= $self->scan_sqstring(), next if $c eq "'";
		$token .= $self->scan_dqstring(), next if $c eq '"';
		$token .= $c . $self->scan_dollar(), next if $c eq '$';
		$self->{lineno}++, $self->swallow_heredocs(), $token = $c, last if $c eq "\n";
		$token = $self->scan_op($c), last if $c =~ /^[;&|<>]$/;
		$token = $c, last if $c =~ /^[(){}]$/;
		if ($c eq '\\') {
			$token .= '\\', last unless $$b =~ /\G(.)/sgc;
			$c = $1;
			$self->{lineno}++, next if $c eq "\n" && length($token); # line splice
			$self->{lineno}++, goto RESTART if $c eq "\n"; # line splice
			$token .= '\\' . $c;
			next;
		}
		die("internal error scanning character '$c'\n");
	}
	return length($token) ? [$token, $start, pos($$b), $startln, $self->{lineno}] : undef;
}

# ShellParser parses POSIX shell scripts (with minor extensions for Bash). It
# is a recursive descent parser very roughly modeled after section 2.10 "Shell
# Grammar" of POSIX chapter 2 "Shell Command Language".
package ShellParser;

sub new {
	my ($class, $s) = @_;
	my $self = bless {
		buff => [],
		stop => [],
		output => []
	} => $class;
	$self->{lexer} = Lexer->new($self, $s);
	return $self;
}

sub next_token {
	my $self = shift @_;
	return pop(@{$self->{buff}}) if @{$self->{buff}};
	return $self->{lexer}->scan_token();
}

sub untoken {
	my $self = shift @_;
	push(@{$self->{buff}}, @_);
}

sub peek {
	my $self = shift @_;
	my $token = $self->next_token();
	return undef unless defined($token);
	$self->untoken($token);
	return $token;
}

sub stop_at {
	my ($self, $token) = @_;
	return 1 unless defined($token);
	my $stop = ${$self->{stop}}[-1] if @{$self->{stop}};
	return defined($stop) && $token->[0] =~ $stop;
}

sub expect {
	my ($self, $expect) = @_;
	my $token = $self->next_token();
	return $token if defined($token) && $token->[0] eq $expect;
	push(@{$self->{output}}, "?!ERR?! expected '$expect' but found '" . (defined($token) ? $token->[0] : "<end-of-input>") . "'\n");
	$self->untoken($token) if defined($token);
	return ();
}

sub optional_newlines {
	my $self = shift @_;
	my @tokens;
	while (my $token = $self->peek()) {
		last unless $token->[0] eq "\n";
		push(@tokens, $self->next_token());
	}
	return @tokens;
}

sub parse_group {
	my $self = shift @_;
	return ($self->parse(qr/^}$/),
		$self->expect('}'));
}

sub parse_subshell {
	my $self = shift @_;
	return ($self->parse(qr/^\)$/),
		$self->expect(')'));
}

sub parse_case_pattern {
	my $self = shift @_;
	my @tokens;
	while (defined(my $token = $self->next_token())) {
		push(@tokens, $token);
		last if $token->[0] eq ')';
	}
	return @tokens;
}

sub parse_case {
	my $self = shift @_;
	my @tokens;
	push(@tokens,
	     $self->next_token(), # subject
	     $self->optional_newlines(),
	     $self->expect('in'),
	     $self->optional_newlines());
	while (1) {
		my $token = $self->peek();
		last unless defined($token) && $token->[0] ne 'esac';
		push(@tokens,
		     $self->parse_case_pattern(),
		     $self->optional_newlines(),
		     $self->parse(qr/^(?:;;|esac)$/)); # item body
		$token = $self->peek();
		last unless defined($token) && $token->[0] ne 'esac';
		push(@tokens,
		     $self->expect(';;'),
		     $self->optional_newlines());
	}
	push(@tokens, $self->expect('esac'));
	return @tokens;
}

sub parse_for {
	my $self = shift @_;
	my @tokens;
	push(@tokens,
	     $self->next_token(), # variable
	     $self->optional_newlines());
	my $token = $self->peek();
	if (defined($token) && $token->[0] eq 'in') {
		push(@tokens,
		     $self->expect('in'),
		     $self->optional_newlines());
	}
	push(@tokens,
	     $self->parse(qr/^do$/), # items
	     $self->expect('do'),
	     $self->optional_newlines(),
	     $self->parse_loop_body(),
	     $self->expect('done'));
	return @tokens;
}

sub parse_if {
	my $self = shift @_;
	my @tokens;
	while (1) {
		push(@tokens,
		     $self->parse(qr/^then$/), # if/elif condition
		     $self->expect('then'),
		     $self->optional_newlines(),
		     $self->parse(qr/^(?:elif|else|fi)$/)); # if/elif body
		my $token = $self->peek();
		last unless defined($token) && $token->[0] eq 'elif';
		push(@tokens, $self->expect('elif'));
	}
	my $token = $self->peek();
	if (defined($token) && $token->[0] eq 'else') {
		push(@tokens,
		     $self->expect('else'),
		     $self->optional_newlines(),
		     $self->parse(qr/^fi$/)); # else body
	}
	push(@tokens, $self->expect('fi'));
	return @tokens;
}

sub parse_loop_body {
	my $self = shift @_;
	return $self->parse(qr/^done$/);
}

sub parse_loop {
	my $self = shift @_;
	return ($self->parse(qr/^do$/), # condition
		$self->expect('do'),
		$self->optional_newlines(),
		$self->parse_loop_body(),
		$self->expect('done'));
}

sub parse_func {
	my $self = shift @_;
	return ($self->expect('('),
		$self->expect(')'),
		$self->optional_newlines(),
		$self->parse_cmd()); # body
}

sub parse_bash_array_assignment {
	my $self = shift @_;
	my @tokens = $self->expect('(');
	while (defined(my $token = $self->next_token())) {
		push(@tokens, $token);
		last if $token->[0] eq ')';
	}
	return @tokens;
}

my %compound = (
	'{' => \&parse_group,
	'(' => \&parse_subshell,
	'case' => \&parse_case,
	'for' => \&parse_for,
	'if' => \&parse_if,
	'until' => \&parse_loop,
	'while' => \&parse_loop);

sub parse_cmd {
	my $self = shift @_;
	my $cmd = $self->next_token();
	return () unless defined($cmd);
	return $cmd if $cmd->[0] eq "\n";

	my $token;
	my @tokens = $cmd;
	if ($cmd->[0] eq '!') {
		push(@tokens, $self->parse_cmd());
		return @tokens;
	} elsif (my $f = $compound{$cmd->[0]}) {
		push(@tokens, $self->$f());
	} elsif (defined($token = $self->peek()) && $token->[0] eq '(') {
		if ($cmd->[0] !~ /\w=$/) {
			push(@tokens, $self->parse_func());
			return @tokens;
		}
		my @array = $self->parse_bash_array_assignment();
		$tokens[-1]->[0] .= join(' ', map {$_->[0]} @array);
		$tokens[-1]->[2] = $array[$#array][2] if @array;
	}

	while (defined(my $token = $self->next_token())) {
		$self->untoken($token), last if $self->stop_at($token);
		push(@tokens, $token);
		last if $token->[0] =~ /^(?:[;&\n|]|&&|\|\|)$/;
	}
	push(@tokens, $self->next_token()) if $tokens[-1]->[0] ne "\n" && defined($token = $self->peek()) && $token->[0] eq "\n";
	return @tokens;
}

sub accumulate {
	my ($self, $tokens, $cmd) = @_;
	push(@$tokens, @$cmd);
}

sub parse {
	my ($self, $stop) = @_;
	push(@{$self->{stop}}, $stop);
	goto DONE if $self->stop_at($self->peek());
	my @tokens;
	while (my @cmd = $self->parse_cmd()) {
		$self->accumulate(\@tokens, \@cmd);
		last if $self->stop_at($self->peek());
	}
DONE:
	pop(@{$self->{stop}});
	return @tokens;
}

# TestParser is a subclass of ShellParser which, beyond parsing shell script
# code, is also imbued with semantic knowledge of test construction, and checks
# tests for common problems (such as broken &&-chains) which might hide bugs in
# the tests themselves or in behaviors being exercised by the tests. As such,
# TestParser is only called upon to parse test bodies, not the top-level
# scripts in which the tests are defined.
package TestParser;

use base 'ShellParser';

sub new {
	my $class = shift @_;
	my $self = $class->SUPER::new(@_);
	$self->{problems} = [];
	return $self;
}

sub find_non_nl {
	my $tokens = shift @_;
	my $n = shift @_;
	$n = $#$tokens if !defined($n);
	$n-- while $n >= 0 && $$tokens[$n]->[0] eq "\n";
	return $n;
}

sub ends_with {
	my ($tokens, $needles) = @_;
	my $n = find_non_nl($tokens);
	for my $needle (reverse(@$needles)) {
		return undef if $n < 0;
		$n = find_non_nl($tokens, $n), next if $needle eq "\n";
		return undef if $$tokens[$n]->[0] !~ $needle;
		$n--;
	}
	return 1;
}

sub match_ending {
	my ($tokens, $endings) = @_;
	for my $needles (@$endings) {
		next if @$tokens < scalar(grep {$_ ne "\n"} @$needles);
		return 1 if ends_with($tokens, $needles);
	}
	return undef;
}

sub parse_loop_body {
	my $self = shift @_;
	my @tokens = $self->SUPER::parse_loop_body(@_);
	# did loop signal failure via "|| return" or "|| exit"?
	return @tokens if !@tokens || grep {$_->[0] =~ /^(?:return|exit|\$\?)$/} @tokens;
	# did loop upstream of a pipe signal failure via "|| echo 'impossible
	# text'" as the final command in the loop body?
	return @tokens if ends_with(\@tokens, [qr/^\|\|$/, "\n", qr/^echo$/, qr/^.+$/]);
	# flag missing "return/exit" handling explicit failure in loop body
	my $n = find_non_nl(\@tokens);
	push(@{$self->{problems}}, ['LOOP', $tokens[$n]]);
	return @tokens;
}

my @safe_endings = (
	[qr/^(?:&&|\|\||\||&)$/],
	[qr/^(?:exit|return)$/, qr/^(?:\d+|\$\?)$/],
	[qr/^(?:exit|return)$/, qr/^(?:\d+|\$\?)$/, qr/^;$/],
	[qr/^(?:exit|return|continue)$/],
	[qr/^(?:exit|return|continue)$/, qr/^;$/]);

sub accumulate {
	my ($self, $tokens, $cmd) = @_;
	my $problems = $self->{problems};

	# no previous command to check for missing "&&"
	goto DONE unless @$tokens;

	# new command is empty line; can't yet check if previous is missing "&&"
	goto DONE if @$cmd == 1 && $$cmd[0]->[0] eq "\n";

	# did previous command end with "&&", "|", "|| return" or similar?
	goto DONE if match_ending($tokens, \@safe_endings);

	# if this command handles "$?" specially, then okay for previous
	# command to be missing "&&"
	for my $token (@$cmd) {
		goto DONE if $token->[0] =~ /\$\?/;
	}

	# if this command is "false", "return 1", or "exit 1" (which signal
	# failure explicitly), then okay for all preceding commands to be
	# missing "&&"
	if ($$cmd[0]->[0] =~ /^(?:false|return|exit)$/) {
		@$problems = grep {$_->[0] ne 'AMP'} @$problems;
		goto DONE;
	}

	# flag missing "&&" at end of previous command
	my $n = find_non_nl($tokens);
	push(@$problems, ['AMP', $tokens->[$n]]) unless $n < 0;

DONE:
	$self->SUPER::accumulate($tokens, $cmd);
}

# ScriptParser is a subclass of ShellParser which identifies individual test
# definitions within test scripts, and passes each test body through TestParser
# to identify possible problems. ShellParser detects test definitions not only
# at the top-level of test scripts but also within compound commands such as
# loops and function definitions.
package ScriptParser;

use base 'ShellParser';

sub new {
	my $class = shift @_;
	my $self = $class->SUPER::new(@_);
	$self->{ntests} = 0;
	return $self;
}

# extract the raw content of a token, which may be a single string or a
# composition of multiple strings and non-string character runs; for instance,
# `"test body"` unwraps to `test body`; `word"a b"42'c d'` to `worda b42c d`
sub unwrap {
	my $token = (@_ ? shift @_ : $_)->[0];
	# simple case: 'sqstring' or "dqstring"
	return $token if $token =~ s/^'([^']*)'$/$1/;
	return $token if $token =~ s/^"([^"]*)"$/$1/;

	# composite case
	my ($s, $q, $escaped);
	while (1) {
		# slurp up non-special characters
		$s .= $1 if $token =~ /\G([^\\'"]*)/gc;
		# handle special characters
		last unless $token =~ /\G(.)/sgc;
		my $c = $1;
		$q = undef, next if defined($q) && $c eq $q;
		$q = $c, next if !defined($q) && $c =~ /^['"]$/;
		if ($c eq '\\') {
			last unless $token =~ /\G(.)/sgc;
			$c = $1;
			$s .= '\\' if $c eq "\n"; # preserve line splice
		}
		$s .= $c;
	}
	return $s
}

sub check_test {
	my $self = shift @_;
	my ($title, $body) = map(unwrap, @_);
	$self->{ntests}++;
	my $parser = TestParser->new(\$body);
	my @tokens = $parser->parse();
	my $problems = $parser->{problems};
	return unless $emit_all || @$problems;
	my $c = main::fd_colors(1);
	my $lineno = $_[1]->[3];
	my $start = 0;
	my $checked = '';
	for (sort {$a->[1]->[2] <=> $b->[1]->[2]} @$problems) {
		my ($label, $token) = @$_;
		my $pos = $token->[2];
		$checked .= substr($body, $start, $pos - $start) . " ?!$label?! ";
		$start = $pos;
	}
	$checked .= substr($body, $start);
	$checked =~ s/^/$lineno++ . ' '/mge;
	$checked =~ s/^\d+ \n//;
	$checked =~ s/(\s) \?!/$1?!/mg;
	$checked =~ s/\?! (\s)/?!$1/mg;
	$checked =~ s/(\?![^?]+\?!)/$c->{rev}$c->{red}$1$c->{reset}/mg;
	$checked =~ s/^\d+/$c->{dim}$&$c->{reset}/mg;
	$checked .= "\n" unless $checked =~ /\n$/;
	push(@{$self->{output}}, "$c->{blue}# chainlint: $title$c->{reset}\n$checked");
}

sub parse_cmd {
	my $self = shift @_;
	my @tokens = $self->SUPER::parse_cmd();
	return @tokens unless @tokens && $tokens[0]->[0] =~ /^test_expect_(?:success|failure)$/;
	my $n = $#tokens;
	$n-- while $n >= 0 && $tokens[$n]->[0] =~ /^(?:[;&\n|]|&&|\|\|)$/;
	$self->check_test($tokens[1], $tokens[2]) if $n == 2; # title body
	$self->check_test($tokens[2], $tokens[3]) if $n > 2;  # prereq title body
	return @tokens;
}

# main contains high-level functionality for processing command-line switches,
# feeding input test scripts to ScriptParser, and reporting results.
package main;

my $getnow = sub { return time(); };
my $interval = sub { return time() - shift; };
if (eval {require Time::HiRes; Time::HiRes->import(); 1;}) {
	$getnow = sub { return [Time::HiRes::gettimeofday()]; };
	$interval = sub { return Time::HiRes::tv_interval(shift); };
}

# Restore TERM if test framework set it to "dumb" so 'tput' will work; do this
# outside of get_colors() since under 'ithreads' all threads use %ENV of main
# thread and ignore %ENV changes in subthreads.
$ENV{TERM} = $ENV{USER_TERM} if $ENV{USER_TERM};

my @NOCOLORS = (bold => '', rev => '', dim => '', reset => '', blue => '', green => '', red => '');
my %COLORS = ();
sub get_colors {
	return \%COLORS if %COLORS;
	if (exists($ENV{NO_COLOR})) {
		%COLORS = @NOCOLORS;
		return \%COLORS;
	}
	if ($ENV{TERM} =~ /xterm|xterm-\d+color|xterm-new|xterm-direct|nsterm|nsterm-\d+color|nsterm-direct/) {
		%COLORS = (bold  => "\e[1m",
			   rev   => "\e[7m",
			   dim   => "\e[2m",
			   reset => "\e[0m",
			   blue  => "\e[34m",
			   green => "\e[32m",
			   red   => "\e[31m");
		return \%COLORS;
	}
	if (system("tput sgr0 >/dev/null 2>&1") == 0 &&
	    system("tput bold >/dev/null 2>&1") == 0 &&
	    system("tput rev  >/dev/null 2>&1") == 0 &&
	    system("tput dim  >/dev/null 2>&1") == 0 &&
	    system("tput setaf 1 >/dev/null 2>&1") == 0) {
		%COLORS = (bold  => `tput bold`,
			   rev   => `tput rev`,
			   dim   => `tput dim`,
			   reset => `tput sgr0`,
			   blue  => `tput setaf 4`,
			   green => `tput setaf 2`,
			   red   => `tput setaf 1`);
		return \%COLORS;
	}
	%COLORS = @NOCOLORS;
	return \%COLORS;
}

my %FD_COLORS = ();
sub fd_colors {
	my $fd = shift;
	return $FD_COLORS{$fd} if exists($FD_COLORS{$fd});
	$FD_COLORS{$fd} = -t $fd ? get_colors() : {@NOCOLORS};
	return $FD_COLORS{$fd};
}

sub ncores {
	# Windows
	return $ENV{NUMBER_OF_PROCESSORS} if exists($ENV{NUMBER_OF_PROCESSORS});
	# Linux / MSYS2 / Cygwin / WSL
	do { local @ARGV='/proc/cpuinfo'; return scalar(grep(/^processor[\s\d]*:/, <>)); } if -r '/proc/cpuinfo';
	# macOS & BSD
	return qx/sysctl -n hw.ncpu/ if $^O =~ /(?:^darwin$|bsd)/;
	return 1;
}

sub show_stats {
	my ($start_time, $stats) = @_;
	my $walltime = $interval->($start_time);
	my ($usertime) = times();
	my ($total_workers, $total_scripts, $total_tests, $total_errs) = (0, 0, 0, 0);
	my $c = fd_colors(2);
	print(STDERR $c->{green});
	for (@$stats) {
		my ($worker, $nscripts, $ntests, $nerrs) = @$_;
		print(STDERR "worker $worker: $nscripts scripts, $ntests tests, $nerrs errors\n");
		$total_workers++;
		$total_scripts += $nscripts;
		$total_tests += $ntests;
		$total_errs += $nerrs;
	}
	printf(STDERR "total: %d workers, %d scripts, %d tests, %d errors, %.2fs/%.2fs (wall/user)$c->{reset}\n", $total_workers, $total_scripts, $total_tests, $total_errs, $walltime, $usertime);
}

sub check_script {
	my ($id, $next_script, $emit) = @_;
	my ($nscripts, $ntests, $nerrs) = (0, 0, 0);
	while (my $path = $next_script->()) {
		$nscripts++;
		my $fh;
		unless (open($fh, "<", $path)) {
			$emit->("?!ERR?! $path: $!\n");
			next;
		}
		my $s = do { local $/; <$fh> };
		close($fh);
		my $parser = ScriptParser->new(\$s);
		1 while $parser->parse_cmd();
		if (@{$parser->{output}}) {
			my $c = fd_colors(1);
			my $s = join('', @{$parser->{output}});
			$emit->("$c->{bold}$c->{blue}# chainlint: $path$c->{reset}\n" . $s);
			$nerrs += () = $s =~ /\?![^?]+\?!/g;
		}
		$ntests += $parser->{ntests};
	}
	return [$id, $nscripts, $ntests, $nerrs];
}

sub exit_code {
	my $stats = shift @_;
	for (@$stats) {
		my ($worker, $nscripts, $ntests, $nerrs) = @$_;
		return 1 if $nerrs;
	}
	return 0;
}

Getopt::Long::Configure(qw{bundling});
GetOptions(
	"emit-all!" => \$emit_all,
	"jobs|j=i" => \$jobs,
	"stats|show-stats!" => \$show_stats) or die("option error\n");
$jobs = ncores() if $jobs < 1;

my $start_time = $getnow->();
my @stats;

my @scripts;
push(@scripts, File::Glob::bsd_glob($_)) for (@ARGV);
unless (@scripts) {
	show_stats($start_time, \@stats) if $show_stats;
	exit;
}

unless ($Config{useithreads} && eval {
	require threads; threads->import();
	require Thread::Queue; Thread::Queue->import();
	1;
	}) {
	push(@stats, check_script(1, sub { shift(@scripts); }, sub { print(@_); }));
	show_stats($start_time, \@stats) if $show_stats;
	exit(exit_code(\@stats));
}

my $script_queue = Thread::Queue->new();
my $output_queue = Thread::Queue->new();

sub next_script { return $script_queue->dequeue(); }
sub emit { $output_queue->enqueue(@_); }

sub monitor {
	while (my $s = $output_queue->dequeue()) {
		print($s);
	}
}

my $mon = threads->create({'context' => 'void'}, \&monitor);
threads->create({'context' => 'list'}, \&check_script, $_, \&next_script, \&emit) for 1..$jobs;

$script_queue->enqueue(@scripts);
$script_queue->end();

for (threads->list()) {
	push(@stats, $_->join()) unless $_ == $mon;
}

$output_queue->end();
$mon->join();

show_stats($start_time, \@stats) if $show_stats;
exit(exit_code(\@stats));
