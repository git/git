##Git Server Docker Setup##
#This File provides instructions to setup a Docker image for a Git server. The image includes SSH and Git, allowing you to create a Git repository within a Docker container and access it out side the container.

##Build Docker Image
docker build -t git-builder .

##Run Docker Container

docker run -p 22:22 -p 80:80 -p 443:443 --name git-server -it git-builder /bin/bash

##Check SSH Service Status
docker exec -it git-server service ssh status

#If the sshd service is not running, execute the following command in the Docker container's bash:
/usr/sbin/sshd -D

##Create Git Repository
#To create a Git repository, run the following commands in the Docker container's bash:

mkdir /usr/local/gitrep
cd /usr/local/gitrep
git init --bare
chown -R git:git /usr/local/gitrep
Clone Repository

#You can now clone the repository using the following command:
git clone git@localhost:/usr/local/gitrep
