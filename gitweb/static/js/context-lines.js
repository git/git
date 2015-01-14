
/**
 *  Called from window.onload, see git_footer_html in gitweb.perl/cgi
 *  
 *  Changes the current value of the "context lines" selectbox to the value in 
 *  the url (if any).
 *
*/
function contextLinesSelectboxSetup(){
	var selectBox = document.getElementById('contextLinesSelector');
	if (! selectBox ){
		return null;
	}
	selectBox.onchange=function() { changedContextLinesRefreshPage(selectBox) };


	// If we came here with a u= param, set up the page to reflect it
	// first
	var matchesArray = document.location.href.match(/[&;]u=[0-9]+/);
	if (! matchesArray){
		return null;
	}
	var incomingValue = matchesArray[0];
	incomingValue = incomingValue.replace(/[&;]u=/, '');
	var thisElement;
	for (var i=0; i<selectBox.length; ++i) {
		thisElement = selectBox[i];
		if (thisElement.value == incomingValue){
			thisElement.selected = true;
		}else{
			thisElement.selected = false;
		}
	}
}

/**
 *  Called when the user changes the number in the "lines of context" selectbox.
 *  
 *  Reloads the page with the new number of lines as u=NNN in the url.
 *
*/
function changedContextLinesRefreshPage(selectBox){
	var selectedIndex = selectBox.selectedIndex;
	var selectedEl = selectBox[selectedIndex];
	var selectedVal = selectedEl.value;

	var href = document.location.href;
	href = href.replace(/[&;]u=[0-9]+/g, '');
	href = href.concat('&u=' + selectedVal);

	document.location.href = href;
}
