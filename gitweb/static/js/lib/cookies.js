/**
 * @fileOverview Accessing cookies from JavaScript
 * @license GPLv2 or later
 */

/*
 * Based on subsection "Cookies in JavaScript" of "Professional
 * JavaScript for Web Developers" by Nicholas C. Zakas and cookie
 * plugin from jQuery (dual licensed under the MIT and GPL licenses)
 */


/**
 * Create a cookie with the given name and value,
 * and other optional parameters.
 *
 * @example
 *   setCookie('foo', 'bar'); // will be deleted when browser exits
 *   setCookie('foo', 'bar', { expires: new Date(Date.parse('Jan 1, 2012')) });
 *   setCookie('foo', 'bar', { expires: 7 }); // 7 days = 1 week
 *   setCookie('foo', 'bar', { expires: 14, path: '/' });
 *
 * @param {String} sName:    Unique name of a cookie (letters, numbers, underscores).
 * @param {String} sValue:   The string value stored in a cookie.
 * @param {Object} [options] An object literal containing key/value pairs
 *                           to provide optional cookie attributes.
 * @param {String|Number|Date} [options.expires] Either literal string to be used as cookie expires,
 *                            or an integer specifying the expiration date from now on in days,
 *                            or a Date object to be used as cookie expiration date.
 *                            If a negative value is specified or a date in the past),
 *                            the cookie will be deleted.
 *                            If set to null or omitted, the cookie will be a session cookie
 *                            and will not be retained when the browser exits.
 * @param {String} [options.path] Restrict access of a cookie to particular directory
 *                               (default: path of page that created the cookie).
 * @param {String} [options.domain] Override what web sites are allowed to access cookie
 *                                  (default: domain of page that created the cookie).
 * @param {Boolean} [options.secure] If true, the secure attribute of the cookie will be set
 *                                   and the cookie would be accessible only from secure sites
 *                                   (cookie transmission will require secure protocol like HTTPS).
 */
function setCookie(sName, sValue, options) {
	options = options || {};
	if (sValue === null) {
		sValue = '';
		option.expires = 'delete';
	}

	var sCookie = sName + '=' + encodeURIComponent(sValue);

	if (options.expires) {
		var oExpires = options.expires, sDate;
		if (oExpires === 'delete') {
			sDate = 'Thu, 01 Jan 1970 00:00:00 GMT';
		} else if (typeof oExpires === 'string') {
			sDate = oExpires;
		} else {
			var oDate;
			if (typeof oExpires === 'number') {
				oDate = new Date();
				oDate.setTime(oDate.getTime() + (oExpires * 24 * 60 * 60 * 1000)); // days to ms
			} else {
				oDate = oExpires;
			}
			sDate = oDate.toGMTString();
		}
		sCookie += '; expires=' + sDate;
	}

	if (options.path) {
		sCookie += '; path=' + (options.path);
	}
	if (options.domain) {
		sCookie += '; domain=' + (options.domain);
	}
	if (options.secure) {
		sCookie += '; secure';
	}
	document.cookie = sCookie;
}

/**
 * Get the value of a cookie with the given name.
 *
 * @param {String} sName: Unique name of a cookie (letters, numbers, underscores)
 * @returns {String|null} The string value stored in a cookie
 */
function getCookie(sName) {
	var sRE = '(?:; )?' + sName + '=([^;]*);?';
	var oRE = new RegExp(sRE);
	if (oRE.test(document.cookie)) {
		return decodeURIComponent(RegExp['$1']);
	} else {
		return null;
	}
}

/**
 * Delete cookie with given name
 *
 * @param {String} sName:    Unique name of a cookie (letters, numbers, underscores)
 * @param {Object} [options] An object literal containing key/value pairs
 *                           to provide optional cookie attributes.
 * @param {String} [options.path]   Must be the same as when setting a cookie
 * @param {String} [options.domain] Must be the same as when setting a cookie
 */
function deleteCookie(sName, options) {
	options = options || {};
	options.expires = 'delete';

	setCookie(sName, '', options);
}

/* end of cookies.js */
