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
/* Handling browser incompatibilities */

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


/**
 * Insert rule giving specified STYLE to given SELECTOR at the end of
 * first CSS stylesheet.
 *
 * @param {String} selector: CSS selector, e.g. '.class'
 * @param {String} style: rule contents, e.g. 'background-color: red;'
 */
function addCssRule(selector, style) {
	var stylesheet = document.styleSheets[0];

	var theRules = [];
	if (stylesheet.cssRules) {     // W3C way
		theRules = stylesheet.cssRules;
	} else if (stylesheet.rules) { // IE way
		theRules = stylesheet.rules;
	}

	if (stylesheet.insertRule) {    // W3C way
		stylesheet.insertRule(selector + ' { ' + style + ' }', theRules.length);
	} else if (stylesheet.addRule) { // IE way
		stylesheet.addRule(selector, style);
	}
}


/* ............................................................ */
/* Support for legacy browsers */

/**
 * Provides getElementsByClassName method, if there is no native
 * implementation of this method.
 *
 * NOTE that there are limits and differences compared to native
 * getElementsByClassName as defined by e.g.:
 *   https://developer.mozilla.org/en/DOM/document.getElementsByClassName
 *   http://www.whatwg.org/specs/web-apps/current-work/multipage/dom.html#dom-getelementsbyclassname
 *   http://www.whatwg.org/specs/web-apps/current-work/multipage/dom.html#dom-document-getelementsbyclassname
 *
 * Namely, this implementation supports only single class name as
 * argument and not set of space-separated tokens representing classes,
 * it returns Array of nodes rather than live NodeList, and has
 * additional optional argument where you can limit search to given tags
 * (via getElementsByTagName).
 *
 * Based on
 *   http://code.google.com/p/getelementsbyclassname/
 *   http://www.dustindiaz.com/getelementsbyclass/
 *   http://stackoverflow.com/questions/1818865/do-we-have-getelementsbyclassname-in-javascript
 *
 * See also http://ejohn.org/blog/getelementsbyclassname-speed-comparison/
 *
 * @param {String} class: name of _single_ class to find
 * @param {String} [taghint] limit search to given tags
 * @returns {Node[]} array of matching elements
 */
if (!('getElementsByClassName' in document)) {
	document.getElementsByClassName = function (classname, taghint) {
		taghint = taghint || "*";
		var elements = (taghint === "*" && document.all) ?
		               document.all :
		               document.getElementsByTagName(taghint);
		var pattern = new RegExp("(^|\\s)" + classname + "(\\s|$)");
		var matches= [];
		for (var i = 0, j = 0, n = elements.length; i < n; i++) {
			var el= elements[i];
			if (el.className && pattern.test(el.className)) {
				// matches.push(el);
				matches[j] = el;
				j++;
			}
		}
		return matches;
	};
} // end if


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
