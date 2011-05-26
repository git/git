// Copyright (C) 2007, Fredrik Kuivinen <frekui@gmail.com>
//               2007, Petr Baudis <pasky@suse.cz>
//          2008-2011, Jakub Narebski <jnareb@gmail.com>

/**
 * @fileOverview Detect if JavaScript is enabled, and pass it to server-side
 * @license GPLv2 or later
 */


/* ============================================================ */
/* Manipulating links */

/**
 * used to check if link has 'js' query parameter already (at end),
 * and other reasons to not add 'js=1' param at the end of link
 * @constant
 */
var jsExceptionsRe = /[;?]js=[01]$/;

/**
 * Add '?js=1' or ';js=1' to the end of every link in the document
 * that doesn't have 'js' query parameter set already.
 *
 * Links with 'js=1' lead to JavaScript version of given action, if it
 * exists (currently there is only 'blame_incremental' for 'blame')
 *
 * To be used as `window.onload` handler
 *
 * @globals jsExceptionsRe
 */
function fixLinks() {
	var allLinks = document.getElementsByTagName("a") || document.links;
	for (var i = 0, len = allLinks.length; i < len; i++) {
		var link = allLinks[i];
		if (!jsExceptionsRe.test(link)) { // =~ /[;?]js=[01]$/;
			link.href +=
				(link.href.indexOf('?') === -1 ? '?' : ';') + 'js=1';
		}
	}
}

/* end of javascript-detection.js */
