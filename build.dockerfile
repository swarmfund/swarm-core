FROM ubuntu:18.04

RUN true \
 && apt-get update \
 && apt-get install -y \
      autoconf \
      automake \
      bison \
      flex \
      git \
      gcc \
      g++ \
      libtool \
      libstdc++6 \
      libpq-dev \
      libssl1.0-dev \
      pkg-config \
      cmake \
      make

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
 && git submodule update --init \
 && cmake CMakeLists.txt -DPostgreSQL_INCLUDE_DIRS=/usr/include/postgresql/ -DPostgreSQL_LIBRARIES=/usr/lib/x86_64-linux-gnu/libpq.so \
 && make -j4


FROM ubuntu:18.04

COPY --from=0 /build/src/core /usr/local/bin/stellar-core
COPY --from=0 /build/entrypoint.sh /entrypoint.sh

RUN true \
 && apt update \
 && apt install --no-install-recommends -y libpq5 libssl1.0.0 awscli s3cmd \
 && rm -rf /var/lib/apt/lists/* /var/log/*.log /var/log/*/*.log \
 && mkdir /data \
 && chmod +x /entrypoint.sh 

VOLUME /data

ENTRYPOINT ["/entrypoint.sh"]
