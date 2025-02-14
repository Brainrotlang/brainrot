FROM ubuntu:latest

RUN apt-get update && apt-get install -y \
    gcc \
    flex \
    bison \
    make \
    libfl-dev \
    python3 \
    python3-venv \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /brainrot

VOLUME /brainrot
