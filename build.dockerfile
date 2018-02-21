FROM ubuntu:16.04

# TODO make base image out of it
RUN true \
 && apt-get update \
 && apt-get install -y \
	git \
	build-essential \
	pkg-config \
	autoconf \
	automake \
	libtool \
	bison \
	flex \
	libpq-dev \
	gcc \
	g++ \
	cpp

ARG RSA_KEY
WORKDIR /build
COPY . $PWD

RUN true \
 && eval $(ssh-agent -s) \
 && echo "$RSA_KEY" | ssh-add - \
 && mkdir -p ~/.ssh \
 && echo "$RSA_KEY" > ~/.ssh/id_rsa \
 && chmod 600 ~/.ssh/id_rsa \
 && echo "Host *\n\tStrictHostKeyChecking no\n\n" > ~/.ssh/config \
 && echo "Host gitlab\n\tHostName gitlab.com\n\tIdentityFile ~/.ssh/id_rsa\n\tUser git\n" >> ~/.ssh/config \
 && git config --global url.ssh://git@gitlab.com/.insteadOf https://gitlab.com/ \
 && git submodule init \
 && git submodule update \
 && ./autogen.sh \
 && ./configure \
 && make -j 4 \
 && mkdir /data \
 && chmod +x ./run.docker

VOLUME /data

ENTRYPOINT ["./run.docker"]
