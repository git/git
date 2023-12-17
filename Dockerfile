FROM ubuntu:latest



# Install build dependencies
# Install build dependencies
RUN export DEBIAN_FRONTEND=noninteractive && \
apt-get update && \
apt-get install -y \
build-essential \
curl \
gettext \
libcurl4-openssl-dev \
libexpat1-dev \
libssl-dev \
libz-dev \
perl \
tcl \
tk \
xmlto \
asciidoc \
docbook2x \
install-info \
openssh-client \
openssh-server

# Generate SSH key pair
RUN ssh-keygen -t rsa -b 4096 -f /root/.ssh/id_rsa -N ""

# Create the missing directory
RUN mkdir -p /run/sshd

# Create git user with password 1234
RUN useradd -m -p "$(openssl passwd -1 1234)" git

# Allow only the git user to SSH into the container
RUN echo "AllowUsers git" >> /etc/ssh/sshd_config

COPY . /usr/src/git-source



WORKDIR /usr/src/git-source



# Build and install Git`chatchatch
RUN make prefix=/usr all doc info && \
make prefix=/usr install install-doc install-html install-info



# Cleanup unnecessary build dependencies
RUN apt-get remove -y \
build-essential \
curl \
gettext \
libcurl4-openssl-dev \
libexpat1-dev \
libssl-dev \
libz-dev && \
apt-get autoremove -y && \
apt-get clean && \
rm -rf /var/lib/apt/lists/* /usr/src/git-source



# Expose SSH port
EXPOSE 22
# Expose HTTP port
EXPOSE 80
# Expose HTTPS port
EXPOSE 443
# Expose Git Protocol port
EXPOSE 9418

# Start the SSH server when the container starts
CMD ["/usr/sbin/sshd", "-D"]
