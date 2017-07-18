// Copyright (C) 2011, John 'Warthog9' Hawley <warthog9@eaglescrag.net>
//               2011, Jakub Narebski <jnareb@gmail.com>

/**
 * @fileOverview Manipulate dates in gitweb output, adjusting timezone
 * @license GPLv2 or later
 */

/**
 * Get common timezone, add UI for changing timezones, and adjust
 * dates to use requested common timezone.
 *
 * This function is called during onload event (added to window.onload).
 *
 * @param {String} tzDefault: default timezone, if there is no cookie
 * @param {Object} tzCookieInfo: object literal with info about cookie to store timezone
 * @param {String} tzCookieInfo.name: name of cookie to store timezone
 * @param {String} tzClassName: denotes elements with date to be adjusted
 */
function onloadTZSetup(tzDefault, tzCookieInfo, tzClassName) {
	var tzCookieTZ = getCookie(tzCookieInfo.name, tzCookieInfo);
	var tz = tzDefault;

	if (tzCookieTZ) {
		// set timezone to value saved in a cookie
		tz = tzCookieTZ;
		// refresh cookie, so its expiration counts from last use of gitweb
		setCookie(tzCookieInfo.name, tzCookieTZ, tzCookieInfo);
	}

	// add UI for changing timezone
	addChangeTZ(tz, tzCookieInfo, tzClassName);

	// server-side of gitweb produces datetime in UTC,
	// so if tz is 'utc' there is no need for changes
	var nochange = tz === 'utc';

	// adjust dates to use specified common timezone
	fixDatetimeTZ(tz, tzClassName, nochange);
}


/* ...................................................................... */
/* Changing dates to use requested timezone */

/**
 * Replace RFC-2822 dates contained in SPAN elements with tzClassName
 * CSS class with equivalent dates in given timezone.
 *
 * @param {String} tz: numeric timezone in '(-|+)HHMM' format, or 'utc', or 'local'
 * @param {String} tzClassName: specifies elements to be changed
 * @param {Boolean} nochange: markup for timezone change, but don't change it
 */
function fixDatetimeTZ(tz, tzClassName, nochange) {
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

		curElement.title = 'Click to change timezone';
		if (!nochange) {
			// we use *.firstChild.data (W3C DOM) instead of *.innerHTML
			// as the latter doesn't always work everywhere in every browser
			var epoch = parseRFC2822Date(curElement.firstChild.data);
			var adjusted = formatDateRFC2882(epoch, tz);

			curElement.firstChild.data = adjusted;
		}
	}
}


/* ...................................................................... */
/* Adding triggers, generating timezone menu, displaying and hiding */

/**
 * Adds triggers for UI to change common timezone used for dates in
 * gitweb output: it marks up and/or creates item to click to invoke
 * timezone change UI, creates timezone UI fragment to be attached,
 * and installs appropriate onclick trigger (via event delegation).
 *
 * @param {String} tzSelected: pre-selected timezone,
 *                             'utc' or 'local' or '(-|+)HHMM'
 * @param {Object} tzCookieInfo: object literal with info about cookie to store timezone
 * @param {String} tzClassName: specifies elements to install trigger
 */
function addChangeTZ(tzSelected, tzCookieInfo, tzClassName) {
	// make link to timezone UI discoverable
	addCssRule('.'+tzClassName + ':hover',
	           'text-decoration: underline; cursor: help;');

	// create form for selecting timezone (to be saved in a cookie)
	var tzSelectFragment = document.createDocumentFragment();
	tzSelectFragment = createChangeTZForm(tzSelectFragment,
	                                      tzSelected, tzCookieInfo, tzClassName);

	// event delegation handler for timezone selection UI (clicking on entry)
	// see http://www.nczonline.net/blog/2009/06/30/event-delegation-in-javascript/
	// assumes that there is no existing document.onclick handler
	document.onclick = function onclickHandler(event) {
		//IE doesn't pass in the event object
		event = event || window.event;

		//IE uses srcElement as the target
		var target = event.target || event.srcElement;

		switch (target.className) {
		case tzClassName:
			// don't display timezone menu if it is already displayed
			if (tzSelectFragment.childNodes.length > 0) {
				displayChangeTZForm(target, tzSelectFragment);
			}
			break;
		} // end switch
	};
}

/**
 * Create DocumentFragment with UI for changing common timezone in
 * which dates are shown in.
 *
 * @param {DocumentFragment} documentFragment: where attach UI
 * @param {String} tzSelected: default (pre-selected) timezone
 * @param {Object} tzCookieInfo: object literal with info about cookie to store timezone
 * @returns {DocumentFragment}
 */
function createChangeTZForm(documentFragment, tzSelected, tzCookieInfo, tzClassName) {
	var div = document.createElement("div");
	div.className = 'popup';

	/* '<div class="close-button" title="(click on this box to close)">X</div>' */
	var closeButton = document.createElement('div');
	closeButton.className = 'close-button';
	closeButton.title = '(click on this box to close)';
	closeButton.appendChild(document.createTextNode('X'));
	closeButton.onclick = closeTZFormHandler(documentFragment, tzClassName);
	div.appendChild(closeButton);

	/* 'Select timezone: <br clear="all">' */
	div.appendChild(document.createTextNode('Select timezone: '));
	var br = document.createElement('br');
	br.clear = 'all';
	div.appendChild(br);

	/* '<select name="tzoffset">
	 *    ...
	 *    <option value="-0700">UTC-07:00</option>
	 *    <option value="-0600">UTC-06:00</option>
	 *    ...
	 *  </select>' */
	var select = document.createElement("select");
	select.name = "tzoffset";
	//select.style.clear = 'all';
	select.appendChild(generateTZOptions(tzSelected));
	select.onchange = selectTZHandler(documentFragment, tzCookieInfo, tzClassName);
	div.appendChild(select);

	documentFragment.appendChild(div);

	return documentFragment;
}


/**
 * Hide (remove from DOM) timezone change UI, ensuring that it is not
 * garbage collected and that it can be re-enabled later.
 *
 * @param {DocumentFragment} documentFragment: contains detached UI
 * @param {HTMLSelectElement} target: select element inside of UI
 * @param {String} tzClassName: specifies element where UI was installed
 * @returns {DocumentFragment} documentFragment
 */
function removeChangeTZForm(documentFragment, target, tzClassName) {
	// find containing element, where we appended timezone selection UI
	// `target' is somewhere inside timezone menu
	var container = target.parentNode, popup = target;
	while (container &&
	       container.className !== tzClassName) {
		popup = container;
		container = container.parentNode;
	}
	// safety check if we found correct container,
	// and if it isn't deleted already
	if (!container || !popup ||
	    container.className !== tzClassName ||
	    popup.className     !== 'popup') {
		return documentFragment;
	}

	// timezone selection UI was appended as last child
	// see also displayChangeTZForm function
	var removed = popup.parentNode.removeChild(popup);
	if (documentFragment.firstChild !== removed) { // the only child
		// re-append it so it would be available for next time
		documentFragment.appendChild(removed);
	}
	// all of inline style was added by this script
	// it is not really needed to remove it, but it is a good practice
	container.removeAttribute('style');

	return documentFragment;
}


/**
 * Display UI for changing common timezone for dates in gitweb output.
 * To be used from 'onclick' event handler.
 *
 * @param {HTMLElement} target: where to install/display UI
 * @param {DocumentFragment} tzSelectFragment: timezone selection UI
 */
function displayChangeTZForm(target, tzSelectFragment) {
	// for absolute positioning to be related to target element
	target.style.position = 'relative';
	target.style.display = 'inline-block';

	// show/display UI for changing timezone
	target.appendChild(tzSelectFragment);
}


/* ...................................................................... */
/* List of timezones for timezone selection menu */

/**
 * Generate list of timezones for creating timezone select UI
 *
 * @returns {Object[]} list of e.g. { value: '+0100', descr: 'GMT+01:00' }
 */
function generateTZList() {
	var timezones = [
		{ value: "utc",   descr: "UTC/GMT"},
		{ value: "local", descr: "Local (per browser)"}
	];

	// generate all full hour timezones (no fractional timezones)
	for (var x = -12, idx = timezones.length; x <= +14; x++, idx++) {
		var hours = (x >= 0 ? '+' : '-') + padLeft(x >=0 ? x : -x, 2);
		timezones[idx] = { value: hours + '00', descr: 'UTC' + hours + ':00'};
		if (x === 0) {
			timezones[idx].descr = 'UTC\u00B100:00'; // 'UTC&plusmn;00:00'
		}
	}

	return timezones;
}

/**
 * Generate <options> elements for timezone select UI
 *
 * @param {String} tzSelected: default timezone
 * @returns {DocumentFragment} list of options elements to appendChild
 */
function generateTZOptions(tzSelected) {
	var elems = document.createDocumentFragment();
	var timezones = generateTZList();

	for (var i = 0, len = timezones.length; i < len; i++) {
		var tzone = timezones[i];
		var option = document.createElement("option");
		if (tzone.value === tzSelected) {
			option.defaultSelected = true;
		}
		option.value = tzone.value;
		option.appendChild(document.createTextNode(tzone.descr));

		elems.appendChild(option);
	}

	return elems;
}


/* ...................................................................... */
/* Event handlers and/or their generators */

/**
 * Create event handler that select timezone and closes timezone select UI.
 * To be used as $('select[name="tzselect"]').onchange handler.
 *
 * @param {DocumentFragment} tzSelectFragment: timezone selection UI
 * @param {Object} tzCookieInfo: object literal with info about cookie to store timezone
 * @param {String} tzCookieInfo.name: name of cookie to save result of selection
 * @param {String} tzClassName: specifies element where UI was installed
 * @returns {Function} event handler
 */
function selectTZHandler(tzSelectFragment, tzCookieInfo, tzClassName) {
	//return function selectTZ(event) {
	return function (event) {
		event = event || window.event;
		var target = event.target || event.srcElement;

		var selected = target.options.item(target.selectedIndex);
		removeChangeTZForm(tzSelectFragment, target, tzClassName);

		if (selected) {
			selected.defaultSelected = true;
			setCookie(tzCookieInfo.name, selected.value, tzCookieInfo);
			fixDatetimeTZ(selected.value, tzClassName);
		}
	};
}

/**
 * Create event handler that closes timezone select UI.
 * To be used e.g. as $('.closebutton').onclick handler.
 *
 * @param {DocumentFragment} tzSelectFragment: timezone selection UI
 * @param {String} tzClassName: specifies element where UI was installed
 * @returns {Function} event handler
 */
function closeTZFormHandler(tzSelectFragment, tzClassName) {
	//return function closeTZForm(event) {
	return function (event) {
		event = event || window.event;
		var target = event.target || event.srcElement;

		removeChangeTZForm(tzSelectFragment, target, tzClassName);
	};
}

/* end of adjust-timezone.js */
