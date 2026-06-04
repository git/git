// Copyright (C) 2007, Fredrik Kuivinen <frekui@gmail.com>
//               2007, Petr Baudis <pasky@suse.cz>
//          2008-2011, Jakub Narebski <jnareb@gmail.com>

/**
 * @fileOverview Datetime manipulation: parsing and formatting
 * @license GPLv2 or later
 */


/* ............................................................ */
/* parsing and retrieving datetime related information */

/**
 * used to extract hours and minutes from timezone info, e.g '-0900'
 * @constant
 */
var tzRe = /^([+\-])([0-9][0-9])([0-9][0-9])$/;

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
 * return local (browser) timezone as offset from UTC in seconds
 *
 * @returns {Number} offset from UTC in seconds for local timezone
 */
function localTimezoneOffset() {
	// getTimezoneOffset returns the time-zone offset from UTC,
	// in _minutes_, for the current locale
	return ((new Date()).getTimezoneOffset() * -60);
}

/**
 * return local (browser) timezone as numeric timezone '(+|-)HHMM'
 *
 * @returns {String} locat timezone as -/+ZZZZ
 */
function localTimezoneInfo() {
	var tzOffsetMinutes = (new Date()).getTimezoneOffset() * -1;

	return formatTimezoneInfo(0, tzOffsetMinutes);
}


/**
 * Parse RFC-2822 date into a Unix timestamp (into epoch)
 *
 * @param {String} date: date in RFC-2822 format, e.g. 'Thu, 21 Dec 2000 16:01:07 +0200'
 * @returns {Number} epoch i.e. seconds since '00:00:00 1970-01-01 UTC'
 */
function parseRFC2822Date(date) {
	// Date.parse accepts the IETF standard (RFC 1123 Section 5.2.14 and elsewhere)
	// date syntax, which is defined in RFC 2822 (obsoletes RFC 822)
	// and returns number of _milli_seconds since January 1, 1970, 00:00:00 UTC
	return Date.parse(date) / 1000;
}


/* ............................................................ */
/* formatting date */

/**
 * format timezone offset as numerical timezone '(+|-)HHMM' or '(+|-)HH:MM'
 *
 * @param {Number} hours:    offset in hours, e.g. 2 for '+0200'
 * @param {Number} [minutes] offset in minutes, e.g. 30 for '-4030';
 *                           it is split into hours if not 0 <= minutes < 60,
 *                           for example 1200 would give '+0100';
 *                           defaults to 0
 * @param {String} [sep] separator between hours and minutes part,
 *                       default is '', might be ':' for W3CDTF (rfc-3339)
 * @returns {String} timezone in '(+|-)HHMM' or '(+|-)HH:MM' format
 */
function formatTimezoneInfo(hours, minutes, sep) {
	minutes = minutes || 0; // to be able to use formatTimezoneInfo(hh)
	sep = sep || ''; // default format is +/-ZZZZ

	if (minutes < 0 || minutes > 59) {
		hours = minutes > 0 ? Math.floor(minutes / 60) : Math.ceil(minutes / 60);
		minutes = Math.abs(minutes - 60*hours); // sign of minutes is sign of hours
		// NOTE: this works correctly because there is no UTC-00:30 timezone
	}

	var tzSign = hours >= 0 ? '+' : '-';
	if (hours < 0) {
		hours = -hours; // sign is stored in tzSign
	}

	return tzSign + padLeft(hours, 2, '0') + sep + padLeft(minutes, 2, '0');
}

/**
 * translate 'utc' and 'local' to numerical timezone
 * @param {String} timezoneInfo: might be 'utc' or 'local' (browser)
 */
function normalizeTimezoneInfo(timezoneInfo) {
	switch (timezoneInfo) {
	case 'utc':
		return '+0000';
	case 'local': // 'local' is browser timezone
		return localTimezoneInfo();
	}
	return timezoneInfo;
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

/**
 * return date in local time formatted in rfc-2822 format
 * e.g. 'Thu, 21 Dec 2000 16:01:07 +0200'
 *
 * @param {Number} epoch: seconds since '00:00:00 1970-01-01 UTC'
 * @param {String} timezoneInfo: numeric timezone '(+|-)HHMM'
 * @param {Boolean} [padDay] e.g. 'Sun, 07 Aug' if true, 'Sun, 7 Aug' otherwise
 * @returns {String} date in local time in rfc-2822 format
 */
function formatDateRFC2882(epoch, timezoneInfo, padDay) {
	// A short textual representation of a month, three letters
	var months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
	// A textual representation of a day, three letters
	var days = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
	// date corrected by timezone
	var localDate = new Date(1000 * (epoch +
		timezoneOffset(timezoneInfo)));
	var localDateStr = // e.g. 'Sun, 7 Aug 2005' or 'Sun, 07 Aug 2005'
		days[localDate.getUTCDay()] + ', ' +
		(padDay ? padLeft(localDate.getUTCDate(),2,'0') : localDate.getUTCDate()) + ' ' +
		months[localDate.getUTCMonth()] + ' ' +
		localDate.getUTCFullYear();
	var localTimeStr = // e.g. '21:49:46'
		padLeft(localDate.getUTCHours(),   2, '0') + ':' +
		padLeft(localDate.getUTCMinutes(), 2, '0') + ':' +
		padLeft(localDate.getUTCSeconds(), 2, '0');

	return localDateStr + ' ' + localTimeStr + ' ' + timezoneInfo;
}

/* end of datetime.js */
