#!/bin/sh
#
# Copyright (c) 2012 Felipe Contreras
#

test_description='Test remote-bzr'

. ./test-lib.sh

if ! test_have_prereq PYTHON; then
	skip_all='skipping remote-bzr tests; python not available'
	test_done
fi

if ! "$PYTHON_PATH" -c 'import bzrlib'; then
	skip_all='skipping remote-bzr tests; bzr not available'
	test_done
fi

cmd='
import bzrlib
bzrlib.initialize()
import bzrlib.plugin
bzrlib.plugin.load_plugins()
import bzrlib.plugins.fastimport
'

if ! "$PYTHON_PATH" -c "$cmd"; then
	echo "consider setting BZR_PLUGIN_PATH=$HOME/.bazaar/plugins" 1>&2
	skip_all='skipping remote-bzr tests; bzr-fastimport not available'
	test_done
fi

check () {
	(cd $1 &&
	git log --format='%s' -1 &&
	git symbolic-ref HEAD) > actual &&
	(echo $2 &&
	echo "refs/heads/$3") > expected &&
	test_cmp expected actual
}

bzr whoami "A U Thor <author@example.com>"

test_expect_success 'cloning' '
  (bzr init bzrrepo &&
  cd bzrrepo &&
  echo one > content &&
  bzr add content &&
  bzr commit -m one
  ) &&

  git clone "bzr::$PWD/bzrrepo" gitrepo &&
  check gitrepo one master
'

test_expect_success 'pulling' '
  (cd bzrrepo &&
  echo two > content &&
  bzr commit -m two
  ) &&

  (cd gitrepo && git pull) &&

  check gitrepo two master
'

test_expect_success 'pushing' '
  (cd gitrepo &&
  echo three > content &&
  git commit -a -m three &&
  git push
  ) &&

  echo three > expected &&
  cat bzrrepo/content > actual &&
  test_cmp expected actual
'

test_expect_success 'roundtrip' '
  (cd gitrepo &&
  git pull &&
  git log --format="%s" -1 origin/master > actual) &&
  echo three > expected &&
  test_cmp expected actual &&

  (cd gitrepo && git push && git pull) &&

  (cd bzrrepo &&
  echo four > content &&
  bzr commit -m four
  ) &&

  (cd gitrepo && git pull && git push) &&

  check gitrepo four master &&

  (cd gitrepo &&
  echo five > content &&
  git commit -a -m five &&
  git push && git pull
  ) &&

  (cd bzrrepo && bzr revert) &&

  echo five > expected &&
  cat bzrrepo/content > actual &&
  test_cmp expected actual
'

test_done
