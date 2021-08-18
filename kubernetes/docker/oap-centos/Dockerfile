# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

FROM spark-centos:3.1.1

MAINTAINER The Optimized Analyitics Package for Spark Platform (OAP) Authors https://github.com/Intel-bigdata/OAP/

#oap
ARG OAP_VERSION=1.2-SNAPSHOT
ENV OAP_VERSION                       ${OAP_VERSION}
ENV OAP_HOME              /opt/home/conda/envs/oap-${OAP_VERSION}
ENV http_proxy=http://child-prc.intel.com:913
ENV https_proxy=https://child-prc.intel.com:913


USER root

#Git Config
RUN git config --global https.proxy https://10.239.4.80:913
RUN git config --global http.proxy http://10.239.4.80:913
RUN git config --global http.postBuffer 5m
RUN git config --global --add http.sslVersion tlsv1.2

#install runtime prerequisites
RUN git clone --branch branch-1.2 https://github.com/oap-project/oap-tools.git oap_source && \
  cd /opt/home/oap_source && \
  sh /opt/home/oap_source/dev/install-runtime-dependencies.sh --with-rdma && \
  rm -rf /opt/home/oap_source

# Install OAP conda packages
COPY oap.yml /opt/home/oap.yml
ARG http_proxy=http://child-prc.intel.com:913
ARG https_proxy=http://child-prc.intel.com:913
ENV HTTP_PROXY http://child-prc.intel.com:913
ENV HTTPS_PROXY http://child-prc.intel.com:913

RUN yum install -y python-urllib3

RUN /opt/home/conda/bin/conda config --system --set channel_priority flexible --set ssl_verify false --set  proxy_servers.http http://child-prc.intel.com:913 --set proxy_servers.https http://child-prc.intel.com:913 && \
    /opt/home/conda/bin/conda update -k -v -n base -c defaults conda --insecure && \
    /opt/home/conda/bin/conda env create -v --file /opt/home/oap.yml

# Specify the User that the actual main process will run as
ARG spark_uid=185
USER ${spark_uid}
ENV PKG_CONFIG_PATH=/usr/local/lib64/pkgconfig/:${PKG_CONFIG_PATH}
