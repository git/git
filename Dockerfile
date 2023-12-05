FROM ubuntu:latest

# Install build dependencies
RUN apt-get update && \
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
    asciidoc

# Set the working directory
WORKDIR /git

# Clone the Git repository
RUN git clone https://github.com/git/git.git .

# Build and install Git
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
    rm -rf /var/lib/apt/lists/* /git
# Expose SSH port
EXPOSE 22
# Expose HTTP port
EXPOSE 80
# Expose HTTPS port
EXPOSE 443
# Expose Git Protocol port
EXPOSE 9418 
# Set the default command to run Git
CMD ["git", "--version"]
