<?php
/**
 * This script generates a SQLite database for a MediaWiki version 1.19.0
 * You must specify the login of the admin (argument 1) and its
 * password (argument 2) and the folder where the database file
 * is located (absolute path in argument 3).
 * It is used by the script install-wiki.sh in order to make easy the
 * installation of a MediaWiki.
 *
 * In order to generate a SQLite database file, MediaWiki ask the user
 * to submit some forms in its web browser. This script simulates this
 * behavior though the functions <get> and <submit>
 *
 */
$argc = $_SERVER['argc'];
$argv = $_SERVER['argv'];

$login = $argv[2];
$pass = $argv[3];
$tmp = $argv[4];
$port = $argv[5];

$url = 'http://localhost:'.$port.'/wiki/mw-config/index.php';
$db_dir = urlencode($tmp);
$tmp_cookie = tempnam($tmp, "COOKIE_");
/*
 * Fetchs a page with cURL.
 */
function get($page_name = "") {
	$curl = curl_init();
	$page_name_add = "";
	if ($page_name != "") {
		$page_name_add = '?page='.$page_name;
	}
	$url = $GLOBALS['url'].$page_name_add;
	$tmp_cookie = $GLOBALS['tmp_cookie'];
	curl_setopt($curl, CURLOPT_COOKIEJAR, $tmp_cookie);
	curl_setopt($curl, CURLOPT_RETURNTRANSFER, true);
	curl_setopt($curl, CURLOPT_FOLLOWLOCATION, true);
	curl_setopt($curl, CURLOPT_COOKIEFILE, $tmp_cookie);
	curl_setopt($curl, CURLOPT_HEADER, true);
	curl_setopt($curl, CURLOPT_URL, $url);

	$page = curl_exec($curl);
	if (!$page) {
		die("Could not get page: $url\n");
	}
	curl_close($curl);
	return $page;
}

/*
 * Submits a form with cURL.
 */
function submit($page_name, $option = "") {
	$curl = curl_init();
	$datapost = 'submit-continue=Continue+%E2%86%92';
	if ($option != "") {
		$datapost = $option.'&'.$datapost;
	}
	$url = $GLOBALS['url'].'?page='.$page_name;
	$tmp_cookie = $GLOBALS['tmp_cookie'];
	curl_setopt($curl, CURLOPT_URL, $url);
	curl_setopt($curl, CURLOPT_POST, true);
	curl_setopt($curl, CURLOPT_FOLLOWLOCATION, true);
	curl_setopt($curl, CURLOPT_POSTFIELDS, $datapost);
	curl_setopt($curl, CURLOPT_RETURNTRANSFER, true);
	curl_setopt($curl, CURLOPT_COOKIEJAR, $tmp_cookie);
	curl_setopt($curl, CURLOPT_COOKIEFILE, $tmp_cookie);

	$page = curl_exec($curl);
	if (!$page) {
		die("Could not get page: $url\n");
	}
	curl_close($curl);
	return "$page";
}

/*
 * Here starts this script: simulates the behavior of the user
 * submitting forms to generates the database file.
 * Note this simulation was made for the MediaWiki version 1.19.0,
 * we can't assume it works with other versions.
 *
 */

$page = get();
if (!preg_match('/input type="hidden" value="([0-9]+)" name="LanguageRequestTime"/',
		$page, $matches)) {
	echo "Unexpected content for page downloaded:\n";
	echo "$page";
	die;
};
$timestamp = $matches[1];
$language = "LanguageRequestTime=$timestamp&uselang=en&ContLang=en";
$page = submit('Language', $language);

submit('Welcome');

$db_config = 'DBType=sqlite';
$db_config = $db_config.'&sqlite_wgSQLiteDataDir='.$db_dir;
$db_config = $db_config.'&sqlite_wgDBname='.$argv[1];
submit('DBConnect', $db_config);

$wiki_config = 'config_wgSitename=TEST';
$wiki_config = $wiki_config.'&config__NamespaceType=site-name';
$wiki_config = $wiki_config.'&config_wgMetaNamespace=MyWiki';
$wiki_config = $wiki_config.'&config__AdminName='.$login;

$wiki_config = $wiki_config.'&config__AdminPassword='.$pass;
$wiki_config = $wiki_config.'&config__AdminPassword2='.$pass;

$wiki_config = $wiki_config.'&wiki__configEmail=email%40email.org';
$wiki_config = $wiki_config.'&config__SkipOptional=skip';
submit('Name', $wiki_config);
submit('Install');
submit('Install');

unlink($tmp_cookie);
?>
