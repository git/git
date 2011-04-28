// Copyright (C) 2011, John 'Warthog9' Hawley <warthog9@eaglescrag.net>
//               2011, Jakub Narebski <jnareb@gmail.com>

/**
 * @fileOverview Manipulate dates in gitweb output, adjusting timezone
 * @license GPLv2 or later
 */

/**
 * Get common timezone and adjust dates to use this common timezone.
 *
 * This function is called during onload event (added to window.onload).
 *
 * @param {String} tzDefault: default timezone, if there is no cookie
 * @param {String} tzCookieName: name of cookie to store timezone
 * @param {String} tzClassName: denotes elements with date to be adjusted
 */
function onloadTZSetup(tzDefault, tzCookieName, tzClassName) {
	var tzCookie = getCookie(tzCookieName);
	var tz = tzCookie ? tzCookie : tzDefault;

	// server-side of gitweb produces datetime in UTC,
	// so if tz is 'utc' there is no need for changes
	if (tz !== 'utc') {
		fixDatetimeTZ(tz, tzClassName);
	}
}


/**
 * Replace RFC-2822 dates contained in SPAN elements with tzClassName
 * CSS class with equivalent dates in given timezone.
 *
 * @param {String} tz: numeric timezone in '(-|+)HHMM' format, or 'utc', or 'local'
 * @param {String} tzClassName: specifies elements to be changed
 */
function fixDatetimeTZ(tz, tzClassName) {
	// sanity check, method should be ensured by common-lib.js
	if (!document.getElementsByClassName) {
		return;
	}

	// translate to timezone in '(-|+)HHMM' format
	tz = normalizeTimezoneInfo(tz);

	// NOTE: result of getElementsByClassName should probably be cached
	var classesFound = document.getElementsByClassName(tzClassName, "span");
	for (var i = 0, len = classesFound.length; i < len; i++) {
		var curElement = classesFound[i];

		// we use *.firstChild.data (W3C DOM) instead of *.innerHTML
		// as the latter doesn't always work everywhere in every browser
		var epoch = parseRFC2822Date(curElement.firstChild.data);
		var adjusted = formatDateRFC2882(epoch, tz);

		curElement.firstChild.data = adjusted;
	}
}

/* end of adjust-timezone.js */
