#

our $G = '/opt/packrat/playpen/public/in-place/git';
$GIT = '/home/junio/bin/Linux/git';
$projectroot = $G;
$site_name = 'Gitster Local';
$stylesheet  = '/gitweb.css';
$logo = '/git-logo.png';
$favicon = '/git-favicon.png';
$projects_list = "$G/index/index.aux";

while (my ($k, $v) = each %feature) {
	$feature{$k}{'override'} = 1;
}
$feature{'pathinfo'}{'override'} = 0;
$feature{'pathinfo'}{'default'} = [1];
1;
