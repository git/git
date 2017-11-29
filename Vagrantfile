# -*- mode: ruby -*-
# vi: set ft=ruby :

# This Vagrantfile defines the requirements of a Linux development environment
# to develop/run Git. This environment can be set up conveniently by installing
# Vagrant and VirtualBox and calling "vagrant up" in the /usr/src/git directory.
#
# See https://github.com/git-for-windows/git/wiki/Vagrant for details.

# Vagrantfile API/syntax version. Don't touch unless you know what you're doing!
VAGRANTFILE_API_VERSION = "2"

$provision = <<PROVISION
apt-get update
apt-get install -y make gcc libexpat-dev libcurl4-openssl-dev gettext tk8.6 libsvn-perl

# clean .profile in case we're re-provisioning
n="$(grep -n 'cd /vagrant' < /home/vagrant/.profile 2> /dev/null |
	sed 's/:.*//')"
test -z "$n" || {
	head -n $(($n-1)) < /home/vagrant/.profile > /tmp/.profile
	mv /tmp/.profile /home/vagrant/.profile
}

# add a nice greeting
cat >> /home/vagrant/.profile << \EOF

cd /vagrant/
export PATH=/home/vagrant/bin:$PATH
cat << \TOOEOF

Welcome to the Vagrant setup for Git!
--------------------------------------

To build & install Git, just execute

	make -j NO_PERL_MAKEMAKER=t install

For more information, see https://github.com/git-for-windows/git/wiki/Vagrant
TOOEOF
EOF

cat << EOF

Now that everything is set up, connect to the Vagrant machine with the command:

	vagrant ssh

EOF
PROVISION

Vagrant.configure(VAGRANTFILE_API_VERSION) do |config|
  config.vm.box = "ubuntu/trusty64"
  config.vm.box_url = "https://atlas.hashicorp.com/ubuntu/boxes/trusty64"

  config.vm.provision :shell, :inline => $provision
end
