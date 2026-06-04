// Copyright (C) 2007, Fredrik Kuivinen <frekui@gmail.com>
//               2007, Petr Baudis <pasky@suse.cz>
//          2008-2011, Jakub Narebski <jnareb@gmail.com>

/**
 * @fileOverview JavaScript side of Ajax-y 'blame_incremental' view in gitweb
 * @license GPLv2 or later
 */

/* ============================================================ */
/*
 * This code uses DOM methods instead of (nonstandard) innerHTML
 * to modify page.
 *
 * innerHTML is non-standard IE extension, though supported by most
 * browsers; however Firefox up to version 1.5 didn't implement it in
 * a strict mode (application/xml+xhtml mimetype).
 *
 * Also my simple benchmarks show that using elem.firstChild.data =
 * 'content' is slightly faster than elem.innerHTML = 'content'.  It
 * is however more fragile (text element fragment must exists), and
 * less feature-rich (we cannot add HTML).
 *
 * Note that DOM 2 HTML is preferred over generic DOM 2 Core; the
 * equivalent using DOM 2 Core is usually shown in comments.
 */


/* ............................................................ */
/* utility/helper functions (and variables) */

var projectUrl; // partial query + separator ('?' or ';')

// 'commits' is an associative map. It maps SHA1s to Commit objects.
var commits = {};

/**
 * constructor for Commit objects, used in 'blame'
 * @class Represents a blamed commit
 * @param {String} sha1: SHA-1 identifier of a commit
 */
function Commit(sha1) {
	if (this instanceof Commit) {
		this.sha1 = sha1;
		this.nprevious = 0; /* number of 'previous', effective parents */
	} else {
		return new Commit(sha1);
	}
}

/* ............................................................ */
/* progress info, timing, error reporting */

var blamedLines = 0;
var totalLines  = '???';
var div_progress_bar;
var div_progress_info;

/**
 * Detects how many lines does a blamed file have,
 * This information is used in progress info
 *
 * @returns {Number|String} Number of lines in file, or string '...'
 */
function countLines() {
	var table =
		document.getElementById('blame_table') ||
		document.getElementsByTagName('table')[0];

	if (table) {
		return table.getElementsByTagName('tr').length - 1; // for header
	} else {
		return '...';
	}
}

/**
 * update progress info and length (width) of progress bar
 *
 * @globals div_progress_info, div_progress_bar, blamedLines, totalLines
 */
function updateProgressInfo() {
	if (!div_progress_info) {
		div_progress_info = document.getElementById('progress_info');
	}
	if (!div_progress_bar) {
		div_progress_bar = document.getElementById('progress_bar');
	}
	if (!div_progress_info && !div_progress_bar) {
		return;
	}

	var percentage = Math.floor(100.0*blamedLines/totalLines);

	if (div_progress_info) {
		div_progress_info.firstChild.data  = blamedLines + ' / ' + totalLines +
			' (' + padLeftStr(percentage, 3, '\u00A0') + '%)';
	}

	if (div_progress_bar) {
		//div_progress_bar.setAttribute('style', 'width: '+percentage+'%;');
		div_progress_bar.style.width = percentage + '%';
	}
}


var t_interval_server = '';
var cmds_server = '';
var t0 = new Date();

/**
 * write how much it took to generate data, and to run script
 *
 * @globals t0, t_interval_server, cmds_server
 */
function writeTimeInterval() {
	var info_time = document.getElementById('generating_time');
	if (!info_time || !t_interval_server) {
		return;
	}
	var t1 = new Date();
	info_time.firstChild.data += ' + (' +
		t_interval_server + ' sec server blame_data / ' +
		(t1.getTime() - t0.getTime())/1000 + ' sec client JavaScript)';

	var info_cmds = document.getElementById('generating_cmd');
	if (!info_time || !cmds_server) {
		return;
	}
	info_cmds.firstChild.data += ' + ' + cmds_server;
}

/**
 * show an error message alert to user within page (in progress info area)
 * @param {String} str: plain text error message (no HTML)
 *
 * @globals div_progress_info
 */
function errorInfo(str) {
	if (!div_progress_info) {
		div_progress_info = document.getElementById('progress_info');
	}
	if (div_progress_info) {
		div_progress_info.className = 'error';
		div_progress_info.firstChild.data = str;
	}
}

/* ............................................................ */
/* coloring rows during blame_data (git blame --incremental) run */

/**
 * used to extract N from 'colorN', where N is a number,
 * @constant
 */
var colorRe = /\bcolor([0-9]*)\b/;

/**
 * return N if <tr class="colorN">, otherwise return null
 * (some browsers require CSS class names to begin with letter)
 *
 * @param {HTMLElement} tr: table row element to check
 * @param {String} tr.className: 'class' attribute of tr element
 * @returns {Number|null} N if tr.className == 'colorN', otherwise null
 *
 * @globals colorRe
 */
function getColorNo(tr) {
	if (!tr) {
		return null;
	}
	var className = tr.className;
	if (className) {
		var match = colorRe.exec(className);
		if (match) {
			return parseInt(match[1], 10);
		}
	}
	return null;
}

var colorsFreq = [0, 0, 0];
/**
 * return one of given possible colors (currently least used one)
 * example: chooseColorNoFrom(2, 3) returns 2 or 3
 *
 * @param {Number[]} arguments: one or more numbers
 *        assumes that  1 <= arguments[i] <= colorsFreq.length
 * @returns {Number} Least used color number from arguments
 * @globals colorsFreq
 */
function chooseColorNoFrom() {
	// choose the color which is least used
	var colorNo = arguments[0];
	for (var i = 1; i < arguments.length; i++) {
		if (colorsFreq[arguments[i]-1] < colorsFreq[colorNo-1]) {
			colorNo = arguments[i];
		}
	}
	colorsFreq[colorNo-1]++;
	return colorNo;
}

/**
 * given two neighbor <tr> elements, find color which would be different
 * from color of both of neighbors; used to 3-color blame table
 *
 * @param {HTMLElement} tr_prev
 * @param {HTMLElement} tr_next
 * @returns {Number} color number N such that
 * colorN != tr_prev.className && colorN != tr_next.className
 */
function findColorNo(tr_prev, tr_next) {
	var color_prev = getColorNo(tr_prev);
	var color_next = getColorNo(tr_next);


	// neither of neighbors has color set
	// THEN we can use any of 3 possible colors
	if (!color_prev && !color_next) {
		return chooseColorNoFrom(1,2,3);
	}

	// either both neighbors have the same color,
	// or only one of neighbors have color set
	// THEN we can use any color except given
	var color;
	if (color_prev === color_next) {
		color = color_prev; // = color_next;
	} else if (!color_prev) {
		color = color_next;
	} else if (!color_next) {
		color = color_prev;
	}
	if (color) {
		return chooseColorNoFrom((color % 3) + 1, ((color+1) % 3) + 1);
	}

	// neighbors have different colors
	// THEN there is only one color left
	return (3 - ((color_prev + color_next) % 3));
}

/* ............................................................ */
/* coloring rows like 'blame' after 'blame_data' finishes */

/**
 * returns true if given row element (tr) is first in commit group
 * to be used only after 'blame_data' finishes (after processing)
 *
 * @param {HTMLElement} tr: table row
 * @returns {Boolean} true if TR is first in commit group
 */
function isStartOfGroup(tr) {
	return tr.firstChild.className === 'sha1';
}

/**
 * change colors to use zebra coloring (2 colors) instead of 3 colors
 * concatenate neighbor commit groups belonging to the same commit
 *
 * @globals colorRe
 */
function fixColorsAndGroups() {
	var colorClasses = ['light', 'dark'];
	var linenum = 1;
	var tr, prev_group;
	var colorClass = 0;
	var table =
		document.getElementById('blame_table') ||
		document.getElementsByTagName('table')[0];

	while ((tr = document.getElementById('l'+linenum))) {
	// index origin is 0, which is table header; start from 1
	//while ((tr = table.rows[linenum])) { // <- it is slower
		if (isStartOfGroup(tr, linenum, document)) {
			if (prev_group &&
			    prev_group.firstChild.firstChild.href ===
			            tr.firstChild.firstChild.href) {
				// we have to concatenate groups
				var prev_rows = prev_group.firstChild.rowSpan || 1;
				var curr_rows =         tr.firstChild.rowSpan || 1;
				prev_group.firstChild.rowSpan = prev_rows + curr_rows;
				//tr.removeChild(tr.firstChild);
				tr.deleteCell(0); // DOM2 HTML way
			} else {
				colorClass = (colorClass + 1) % 2;
				prev_group = tr;
			}
		}
		var tr_class = tr.className;
		tr.className = tr_class.replace(colorRe, colorClasses[colorClass]);
		linenum++;
	}
}


/* ============================================================ */
/* main part: parsing response */

/**
 * Function called for each blame entry, as soon as it finishes.
 * It updates page via DOM manipulation, adding sha1 info, etc.
 *
 * @param {Commit} commit: blamed commit
 * @param {Object} group: object representing group of lines,
 *                        which blame the same commit (blame entry)
 *
 * @globals blamedLines
 */
function handleLine(commit, group) {
	/*
	   This is the structure of the HTML fragment we are working
	   with:

	   <tr id="l123" class="">
	     <td class="sha1" title=""><a href=""> </a></td>
	     <td class="linenr"><a class="linenr" href="">123</a></td>
	     <td class="pre"># times (my ext3 doesn&#39;t).</td>
	   </tr>
	*/

	var resline = group.resline;

	// format date and time string only once per commit
	if (!commit.info) {
		/* e.g. 'Kay Sievers, 2005-08-07 21:49:46 +0200' */
		commit.info = commit.author + ', ' +
			formatDateISOLocal(commit.authorTime, commit.authorTimezone);
	}

	// color depends on group of lines, not only on blamed commit
	var colorNo = findColorNo(
		document.getElementById('l'+(resline-1)),
		document.getElementById('l'+(resline+group.numlines))
	);

	// loop over lines in commit group
	for (var i = 0; i < group.numlines; i++, resline++) {
		var tr = document.getElementById('l'+resline);
		if (!tr) {
			break;
		}
		/*
			<tr id="l123" class="">
			  <td class="sha1" title=""><a href=""> </a></td>
			  <td class="linenr"><a class="linenr" href="">123</a></td>
			  <td class="pre"># times (my ext3 doesn&#39;t).</td>
			</tr>
		*/
		var td_sha1  = tr.firstChild;
		var a_sha1   = td_sha1.firstChild;
		var a_linenr = td_sha1.nextSibling.firstChild;

		/* <tr id="l123" class=""> */
		var tr_class = '';
		if (colorNo !== null) {
			tr_class = 'color'+colorNo;
		}
		if (commit.boundary) {
			tr_class += ' boundary';
		}
		if (commit.nprevious === 0) {
			tr_class += ' no-previous';
		} else if (commit.nprevious > 1) {
			tr_class += ' multiple-previous';
		}
		tr.className = tr_class;

		/* <td class="sha1" title="?" rowspan="?"><a href="?">?</a></td> */
		if (i === 0) {
			td_sha1.title = commit.info;
			td_sha1.rowSpan = group.numlines;

			a_sha1.href = projectUrl + 'a=commit;h=' + commit.sha1;
			if (a_sha1.firstChild) {
				a_sha1.firstChild.data = commit.sha1.substr(0, 8);
			} else {
				a_sha1.appendChild(
					document.createTextNode(commit.sha1.substr(0, 8)));
			}
			if (group.numlines >= 2) {
				var fragment = document.createDocumentFragment();
				var br   = document.createElement("br");
				var match = commit.author.match(/\b([A-Z])\B/g);
				if (match) {
					var text = document.createTextNode(
							match.join(''));
				}
				if (br && text) {
					var elem = fragment || td_sha1;
					elem.appendChild(br);
					elem.appendChild(text);
					if (fragment) {
						td_sha1.appendChild(fragment);
					}
				}
			}
		} else {
			//tr.removeChild(td_sha1); // DOM2 Core way
			tr.deleteCell(0); // DOM2 HTML way
		}

		/* <td class="linenr"><a class="linenr" href="?">123</a></td> */
		var linenr_commit =
			('previous' in commit ? commit.previous : commit.sha1);
		var linenr_filename =
			('file_parent' in commit ? commit.file_parent : commit.filename);
		a_linenr.href = projectUrl + 'a=blame_incremental' +
			';hb=' + linenr_commit +
			';f='  + encodeURIComponent(linenr_filename) +
			'#l' + (group.srcline + i);

		blamedLines++;

		//updateProgressInfo();
	}
}

// ----------------------------------------------------------------------

/**#@+
 * @constant
 */
var sha1Re = /^([0-9a-f]{40}) ([0-9]+) ([0-9]+) ([0-9]+)/;
var infoRe = /^([a-z-]+) ?(.*)/;
var endRe  = /^END ?([^ ]*) ?(.*)/;
/**@-*/

var curCommit = new Commit();
var curGroup  = {};

/**
 * Parse output from 'git blame --incremental [...]', received via
 * XMLHttpRequest from server (blamedataUrl), and call handleLine
 * (which updates page) as soon as blame entry is completed.
 *
 * @param {String[]} lines: new complete lines from blamedata server
 *
 * @globals commits, curCommit, curGroup, t_interval_server, cmds_server
 * @globals sha1Re, infoRe, endRe
 */
function processBlameLines(lines) {
	var match;

	for (var i = 0, len = lines.length; i < len; i++) {

		if ((match = sha1Re.exec(lines[i]))) {
			var sha1 = match[1];
			var srcline  = parseInt(match[2], 10);
			var resline  = parseInt(match[3], 10);
			var numlines = parseInt(match[4], 10);

			var c = commits[sha1];
			if (!c) {
				c = new Commit(sha1);
				commits[sha1] = c;
			}
			curCommit = c;

			curGroup.srcline = srcline;
			curGroup.resline = resline;
			curGroup.numlines = numlines;

		} else if ((match = infoRe.exec(lines[i]))) {
			var info = match[1];
			var data = match[2];
			switch (info) {
			case 'filename':
				curCommit.filename = unquote(data);
				// 'filename' information terminates the entry
				handleLine(curCommit, curGroup);
				updateProgressInfo();
				break;
			case 'author':
				curCommit.author = data;
				break;
			case 'author-time':
				curCommit.authorTime = parseInt(data, 10);
				break;
			case 'author-tz':
				curCommit.authorTimezone = data;
				break;
			case 'previous':
				curCommit.nprevious++;
				// store only first 'previous' header
				if (!('previous' in curCommit)) {
					var parts = data.split(' ', 2);
					curCommit.previous    = parts[0];
					curCommit.file_parent = unquote(parts[1]);
				}
				break;
			case 'boundary':
				curCommit.boundary = true;
				break;
			} // end switch

		} else if ((match = endRe.exec(lines[i]))) {
			t_interval_server = match[1];
			cmds_server = match[2];

		} else if (lines[i] !== '') {
			// malformed line

		} // end if (match)

	} // end for (lines)
}

/**
 * Process new data and return pointer to end of processed part
 *
 * @param {String} unprocessed: new data (from nextReadPos)
 * @param {Number} nextReadPos: end of last processed data
 * @return {Number} end of processed data (new value for nextReadPos)
 */
function processData(unprocessed, nextReadPos) {
	var lastLineEnd = unprocessed.lastIndexOf('\n');
	if (lastLineEnd !== -1) {
		var lines = unprocessed.substring(0, lastLineEnd).split('\n');
		nextReadPos += lastLineEnd + 1 /* 1 == '\n'.length */;

		processBlameLines(lines);
	} // end if

	return nextReadPos;
}

/**
 * Handle XMLHttpRequest errors
 *
 * @param {XMLHttpRequest} xhr: XMLHttpRequest object
 * @param {Number} [xhr.pollTimer] ID of the timeout to clear
 *
 * @globals commits
 */
function handleError(xhr) {
	errorInfo('Server error: ' +
		xhr.status + ' - ' + (xhr.statusText || 'Error contacting server'));

	if (typeof xhr.pollTimer === "number") {
		clearTimeout(xhr.pollTimer);
		delete xhr.pollTimer;
	}
	commits = {}; // free memory
}

/**
 * Called after XMLHttpRequest finishes (loads)
 *
 * @param {XMLHttpRequest} xhr: XMLHttpRequest object
 * @param {Number} [xhr.pollTimer] ID of the timeout to clear
 *
 * @globals commits
 */
function responseLoaded(xhr) {
	if (typeof xhr.pollTimer === "number") {
		clearTimeout(xhr.pollTimer);
		delete xhr.pollTimer;
	}

	fixColorsAndGroups();
	writeTimeInterval();
	commits = {}; // free memory
}

/**
 * handler for XMLHttpRequest onreadystatechange event
 * @see startBlame
 *
 * @param {XMLHttpRequest} xhr: XMLHttpRequest object
 * @param {Number} xhr.prevDataLength: previous value of xhr.responseText.length
 * @param {Number} xhr.nextReadPos: start of unread part of xhr.responseText
 * @param {Number} [xhr.pollTimer] ID of the timeout (to reset or cancel)
 * @param {Boolean} fromTimer: if handler was called from timer
 */
function handleResponse(xhr, fromTimer) {

	/*
	 * xhr.readyState
	 *
	 *  Value  Constant (W3C)    Description
	 *  -------------------------------------------------------------------
	 *  0      UNSENT            open() has not been called yet.
	 *  1      OPENED            send() has not been called yet.
	 *  2      HEADERS_RECEIVED  send() has been called, and headers
	 *                           and status are available.
	 *  3      LOADING           Downloading; responseText holds partial data.
	 *  4      DONE              The operation is complete.
	 */

	if (xhr.readyState !== 4 && xhr.readyState !== 3) {
		return;
	}

	// the server returned error
	// try ... catch block is to work around bug in IE8
	try {
		if (xhr.readyState === 3 && xhr.status !== 200) {
			return;
		}
	} catch (e) {
		return;
	}
	if (xhr.readyState === 4 && xhr.status !== 200) {
		handleError(xhr);
		return;
	}

	// In konqueror xhr.responseText is sometimes null here...
	if (xhr.responseText === null) {
		return;
	}


	// extract new whole (complete) lines, and process them
	if (xhr.prevDataLength !== xhr.responseText.length) {
		xhr.prevDataLength = xhr.responseText.length;
		var unprocessed = xhr.responseText.substring(xhr.nextReadPos);
		xhr.nextReadPos = processData(unprocessed, xhr.nextReadPos);
	}

	// did we finish work?
	if (xhr.readyState === 4) {
		responseLoaded(xhr);
		return;
	}

	// if we get from timer, we have to restart it
	// otherwise onreadystatechange gives us partial response, timer not needed
	if (fromTimer) {
		setTimeout(function () {
			handleResponse(xhr, true);
		}, 1000);

	} else if (typeof xhr.pollTimer === "number") {
		clearTimeout(xhr.pollTimer);
		delete xhr.pollTimer;
	}
}

// ============================================================
// ------------------------------------------------------------

/**
 * Incrementally update line data in blame_incremental view in gitweb.
 *
 * @param {String} blamedataUrl: URL to server script generating blame data.
 * @param {String} bUrl: partial URL to project, used to generate links.
 *
 * Called from 'blame_incremental' view after loading table with
 * file contents, a base for blame view.
 *
 * @globals t0, projectUrl, div_progress_bar, totalLines
*/
function startBlame(blamedataUrl, bUrl) {

	var xhr = createRequestObject();
	if (!xhr) {
		errorInfo('ERROR: XMLHttpRequest not supported');
		return;
	}

	t0 = new Date();
	projectUrl = bUrl + (bUrl.indexOf('?') === -1 ? '?' : ';');
	if ((div_progress_bar = document.getElementById('progress_bar'))) {
		//div_progress_bar.setAttribute('style', 'width: 100%;');
		div_progress_bar.style.cssText = 'width: 100%;';
	}
	totalLines = countLines();
	updateProgressInfo();

	/* add extra properties to xhr object to help processing response */
	xhr.prevDataLength = -1;  // used to detect if we have new data
	xhr.nextReadPos = 0;      // where unread part of response starts

	xhr.onreadystatechange = function () {
		handleResponse(xhr, false);
	};

	xhr.open('GET', blamedataUrl);
	xhr.setRequestHeader('Accept', 'text/plain');
	xhr.send(null);

	// not all browsers call onreadystatechange event on each server flush
	// poll response using timer every second to handle this issue
	xhr.pollTimer = setTimeout(function () {
		handleResponse(xhr, true);
	}, 1000);
}

/* end of blame_incremental.js */
