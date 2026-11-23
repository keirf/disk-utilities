# Build this: docker build -t disk-utilities .
# And run it: docker run -it -v "$(pwd):/root/host_data" disk-utilities
# Script by Tom60 <contact.tom60@proton.me>

FROM ubuntu:22.04

# Prevent timezone prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive
ENV HOME=/root

# 1. Install necessary projects dependencies
RUN apt-get update && apt-get install -y \
    make \
    gcc \
    g++ \
    unzip \
    wget \
    git \
    tar \
    bash

WORKDIR $HOME/projects

# 2. Download and install CAPS library headers (v4.2)
RUN wget -O ipflib42_linux-x86_64.tar.gz https://web.archive.org/web/20260528001256id_/http://www.softpres.org/_media/files:ipflib42_linux-x86_64.tar.gz && \
    tar xf ipflib42_linux-x86_64.tar.gz && \
    cp -a x86_64-linux-gnu-capsimage/include/caps /usr/include && \
    rm -rf ipflib42_linux-x86_64.tar.gz x86_64-linux-gnu-capsimage

# 3. Download, projects, and install the SPS Decoder Library
RUN wget https://www.kryoflux.com/download/spsdeclib_5.1_source.zip && \
    unzip spsdeclib_5.1_source.zip && \
    unzip capsimg_source_linux_macosx.zip && \
    cd capsimg_source_linux_macosx/CAPSImg && \
    chmod u+x configure && \
    ./configure && \
    make && \
    make install && \
    cd /usr/local/lib && \
    # Create the symlink dynamically based on the compiled version 
    ln -sf libcapsimage.so.5.* libcapsimage.so.5 && \
    cd $HOME/projects && \
    rm -rf spsdeclib_5.21_source.zip capsimg_source_linux_macosx capsimg_source_linux_macosx.zip

# 4. Clone, projects, and install disk-utilities with CAPS enabled
COPY . $HOME/projects/disk-utilities
RUN cd $HOME/projects/disk-utilities && \
    make clean && \
    caps=y make && \
    caps=y make install && \
    ldconfig && \
    # Clean up the projects directory to keep the image small
    cd $HOME/ && rm -rf $HOME/projects

# 5. Set up a working directory for mounted files
WORKDIR $HOME

# Default command opens an interactive shell
CMD ["/bin/bash"]