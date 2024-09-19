FROM ubuntu:22.04

RUN apt-get update && apt install -y build-essential

WORKDIR /home/ubuntu

COPY peer.c .
COPY hostsfile.txt .

RUN gcc peer.c -o peer