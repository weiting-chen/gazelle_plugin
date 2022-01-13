FROM ubuntu:20.04 as builder
LABEL maintainer="weiting.chen <weiting.chen@intel.com>" \
      description="Gazelle Developer Environment"

ENV DEBIAN_FRONTEND noninteractive
ENV TZ Etc/UTC

# Install required utilities
RUN apt-get update
RUN apt-get install --yes \
        bash \
        sudo \
        tzdata \
        coreutils \
        procps \
        apt-utils \
        curl \
        wget \
        unzip \
        maven \
        git \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# Install OpenJDK-8
RUN apt-get update \
    && apt-get install -y openjdk-8-jdk \
    && apt-get install -y ant \
    && apt-get clean;

# Fix certificate issues
RUN apt-get update \
    && apt-get install ca-certificates-java \
    && apt-get clean \
    && update-ca-certificates -f;

ENV JAVA_HOME	/usr/lib/jvm/java-8-openjdk-amd64/
ENV PATH	${JAVA_HOME}/bin:${PATH}

RUN export JAVA_HOME

# Install Cmake
RUN apt-get update \
    && apt-get install -y \
        cmake \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Install GCC-9
RUN apt-get update \
    && apt-get install -y --no-install-recommends software-properties-common \
    && add-apt-repository ppa:ubuntu-toolchain-r/test \
    && apt-get install -y --no-install-recommends \
        build-essential \
        gcc-9 \
        g++-9 \
        gcc-9-multilib \
        g++-9-multilib \
        xutils-dev \
        patch \
        python3 \
        python3-pip \
        libpulse-dev \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Install LLVM
RUN apt-get update \
    && apt-get install -y \
        llvm \
        clang \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Install LibSSL
RUN apt-get update \
    && apt-get install -y \
        libssl-dev \
        libcurl4-openssl-dev

# Install libZ
RUN apt-get update \
    && apt-get install -y \
        libz-dev

# Install libBoost
RUN DEBIAN_FRONTEND=dialog apt-get update \
    && apt-get install -y \
        libboost-all-dev

# Install Spark3.1.1
WORKDIR /opt/spark
RUN wget http://archive.apache.org/dist/spark/spark-3.1.1/spark-3.1.1-bin-hadoop3.2.tgz
RUN tar -xf spark-3.1.1-bin-hadoop3.2.tgz .
ENV SPARK_HOME /opt/spark/spark-3.1.1-bin-hadoop3.2

# Install Hadoop3.2
WORKDIR /opt/hadoop
RUN wget https://downloads.apache.org/hadoop/common/hadoop-3.2.2/hadoop-3.2.2.tar.gz
RUN tar -xf hadoop-3.2.2.tar.gz .
ENV HADOOP_HOME /opt/hadoop/hadoop-3.2.2

# Install Gazelle

