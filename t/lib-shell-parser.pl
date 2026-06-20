# Copyright (c) 2021-2022 Eric Sunshine <sunshine@sunshineco.com>
#
# Shared shell script parser for test lint tools. Provides Lexer,
# ShellParser, and ScriptParser. Subclass ScriptParser and override
# check_test() to implement lint checks.

use strict;
use warnings;

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
# `1 + 2`, which embeds whitepaces, is scanned as three token in shell, as well.
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
		# Slurp non-special characters; count newlines here because
		# newlines inside $() are already counted by the recursive parse.
		if ($$b =~ /\G([^"\$\\]+)/gc) {
			$s .= $1;
			$self->{lineno} += $1 =~ tr/\n//;
		}
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
			$self->{parser}->{heredocs}->{$$tag[0]} = {
				content => substr($body, 0, length($body) - length($&)),
				start_line => $self->{lineno},
		        };
			$self->{lineno} += () = $body =~ /\n/sg;
			next;
		}
		push(@{$self->{parser}->{problems}}, ['HEREDOC', $tag]);
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
		output => [],
		heredocs => {},
		insubshell => 0,
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
	$self->{insubshell}++;
	my @tokens = ($self->parse(qr/^\)$/),
		      $self->expect(')'));
	$self->{insubshell}--;
	return @tokens;
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

# ScriptParser is a subclass of ShellParser which identifies individual test
# definitions within test scripts and passes each test body to check_test().
# ScriptParser detects test definitions not only at the top-level of test
# scripts but also within compound commands such as loops and function
# definitions.

package ScriptParser;

our @ISA = ('ShellParser');

sub new {
	my $class = shift @_;
	my $self = $class->SUPER::new(@_);
	$self->{ntests} = 0;
	$self->{nerrs} = 0;
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
	# no-op; subclass and override to implement lint checks
}

sub parse_cmd {
	my $self = shift @_;
	my @tokens = $self->SUPER::parse_cmd();
	return @tokens unless @tokens && $tokens[0]->[0] =~ /^test_expect_(?:success|failure)$/;
	my $n = $#tokens;
	$n-- while $n >= 0 && $tokens[$n]->[0] =~ /^(?:[;&\n|]|&&|\|\|)$/;
	my $herebody;
	if ($n >= 2 && $tokens[$n-1]->[0] eq '-' && $tokens[$n]->[0] =~ /^<<-?(.+)$/) {
		$herebody = $self->{heredocs}->{$1};
		$n--;
	}
	$self->check_test($tokens[1], $tokens[2], $herebody) if $n == 2; # title body
	$self->check_test($tokens[2], $tokens[3], $herebody) if $n > 2;  # prereq title body
	return @tokens;
}

1;
