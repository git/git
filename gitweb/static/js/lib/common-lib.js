// Copyright (C) 2007, Fredrik Kuivinen <frekui@gmail.com>
//               2007, Petr Baudis <pasky@suse.cz>
//          2008-2011, Jakub Narebski <jnareb@gmail.com>

/**
 * @fileOverview Generic JavaScript code (helper functions)
 * @license GPLv2 or later
 */


/* ============================================================ */
/* ............................................................ */
/* Padding */

/**
 * pad INPUT on the left with STR that is assumed to have visible
 * width of single character (for example nonbreakable spaces),
 * to WIDTH characters
 *
 * example: padLeftStr(12, 3, '\u00A0') == '\u00A012'
 *          ('\u00A0' is nonbreakable space)
 *
 * @param {Number|String} input: number to pad
 * @param {Number} width: visible width of output
 * @param {String} str: string to prefix to string, defaults to '\u00A0'
 * @returns {String} INPUT prefixed with STR x (WIDTH - INPUT.length)
 */
function padLeftStr(input, width, str) {
	var prefix = '';
	if (typeof str === 'undefined') {
		ch = '\u00A0'; // using '&nbsp;' doesn't work in all browsers
	}

	width -= input.toString().length;
	while (width > 0) {
		prefix += str;
		width--;
	}
	return prefix + input;
}

/**
 * Pad INPUT on the left to WIDTH, using given padding character CH,
 * for example padLeft('a', 3, '_') is '__a'
 *             padLeft(4, 2) is '04' (same as padLeft(4, 2, '0'))
 *
 * @param {String} input: input value converted to string.
 * @param {Number} width: desired length of output.
 * @param {String} ch: single character to prefix to string, defaults to '0'.
 *
 * @returns {String} Modified string, at least SIZE length.
 */
function padLeft(input, width, ch) {
	var s = input + "";
	if (typeof ch === 'undefined') {
		ch = '0';
	}

	while (s.length < width) {
		s = ch + s;
	}
	return s;
}


/* ............................................................ */
/* Ajax */

/**
 * Create XMLHttpRequest object in cross-browser way
 * @returns XMLHttpRequest object, or null
 */
function createRequestObject() {
	try {
		return new XMLHttpRequest();
	} catch (e) {}
	try {
		return window.createRequest();
	} catch (e) {}
	try {
		return new ActiveXObject("Msxml2.XMLHTTP");
	} catch (e) {}
	try {
		return new ActiveXObject("Microsoft.XMLHTTP");
	} catch (e) {}

	return null;
}


/* ............................................................ */
/* time and data */

/**
 * used to extract hours and minutes from timezone info, e.g '-0900'
 * @constant
 */
var tzRe = /^([+-])([0-9][0-9])([0-9][0-9])$/;

/**
 * convert numeric timezone +/-ZZZZ to offset from UTC in seconds
 *
 * @param {String} timezoneInfo: numeric timezone '(+|-)HHMM'
 * @returns {Number} offset from UTC in seconds for timezone
 *
 * @globals tzRe
 */
function timezoneOffset(timezoneInfo) {
	var match = tzRe.exec(timezoneInfo);
	var tz_sign = (match[1] === '-' ? -1 : +1);
	var tz_hour = parseInt(match[2],10);
	var tz_min  = parseInt(match[3],10);

	return tz_sign*(((tz_hour*60) + tz_min)*60);
}

/**
 * return date in local time formatted in iso-8601 like format
 * 'yyyy-mm-dd HH:MM:SS +/-ZZZZ' e.g. '2005-08-07 21:49:46 +0200'
 *
 * @param {Number} epoch: seconds since '00:00:00 1970-01-01 UTC'
 * @param {String} timezoneInfo: numeric timezone '(+|-)HHMM'
 * @returns {String} date in local time in iso-8601 like format
 */
function formatDateISOLocal(epoch, timezoneInfo) {
	// date corrected by timezone
	var localDate = new Date(1000 * (epoch +
		timezoneOffset(timezoneInfo)));
	var localDateStr = // e.g. '2005-08-07'
		localDate.getUTCFullYear()                 + '-' +
		padLeft(localDate.getUTCMonth()+1, 2, '0') + '-' +
		padLeft(localDate.getUTCDate(),    2, '0');
	var localTimeStr = // e.g. '21:49:46'
		padLeft(localDate.getUTCHours(),   2, '0') + ':' +
		padLeft(localDate.getUTCMinutes(), 2, '0') + ':' +
		padLeft(localDate.getUTCSeconds(), 2, '0');

	return localDateStr + ' ' + localTimeStr + ' ' + timezoneInfo;
}


/* ............................................................ */
/* unquoting/unescaping filenames */

/**#@+
 * @constant
 */
var escCodeRe = /\\([^0-7]|[0-7]{1,3})/g;
var octEscRe = /^[0-7]{1,3}$/;
var maybeQuotedRe = /^\"(.*)\"$/;
/**#@-*/

/**
 * unquote maybe C-quoted filename (as used by git, i.e. it is
 * in double quotes '"' if there is any escape character used)
 * e.g. 'aa' -> 'aa', '"a\ta"' -> 'a	a'
 *
 * @param {String} str: git-quoted string
 * @returns {String} Unquoted and unescaped string
 *
 * @globals escCodeRe, octEscRe, maybeQuotedRe
 */
function unquote(str) {
	function unq(seq) {
		var es = {
			// character escape codes, aka escape sequences (from C)
			// replacements are to some extent JavaScript specific
			t: "\t",   // tab            (HT, TAB)
			n: "\n",   // newline        (NL)
			r: "\r",   // return         (CR)
			f: "\f",   // form feed      (FF)
			b: "\b",   // backspace      (BS)
			a: "\x07", // alarm (bell)   (BEL)
			e: "\x1B", // escape         (ESC)
			v: "\v"    // vertical tab   (VT)
		};

		if (seq.search(octEscRe) !== -1) {
			// octal char sequence
			return String.fromCharCode(parseInt(seq, 8));
		} else if (seq in es) {
			// C escape sequence, aka character escape code
			return es[seq];
		}
		// quoted ordinary character
		return seq;
	}

	var match = str.match(maybeQuotedRe);
	if (match) {
		str = match[1];
		// perhaps str = eval('"'+str+'"'); would be enough?
		str = str.replace(escCodeRe,
			function (substr, p1, offset, s) { return unq(p1); });
	}
	return str;
}

/* end of common-lib.js */
