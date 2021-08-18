# Gazelle on Kubernetes
This README contains the script for dockerfile and the script to run thrift server for benchmark like TPC-H, TPC-DS, ...etc.

## Building Spark Image
There are two methods to build Gazelle Docker Image

### Prerequisite
Before building the Docker image, you should
1. Proxy Setting
Please edit the Dockerfile before run docker build.
You can use below method to add your proxy configuration in the Dockerfile. Please make sure to set up 1. http_proxy 2. HTTP_PROXY and 3. https_proxy 4. HTTPS_PROXY variables.
```
ARG http_proxy=http://xxx_proxy_address:port
ENV http_proxy http://xxx_proxy_address:port
```

You can also set up git command to use proxy.
```
RUN git config --global http.proxy http://xxx_proxy_address:port
```

2. For conda update and env create command, if you are facing some SSL issues, please use below method to avoid it.
You can try to turn off the ssl_verify or set up https_proxy to use "http" instead of "https".
```
RUN /opt/home/conda/bin/conda config --system --set channel_priority flexible --set ssl_verify false --set  proxy_servers.http http://xxx_proxy_address:port --set proxy_servers.https http://xxx_proxy_address:port
```

Building a Docker image with Spark3.1.1.
```
docker build --tag spark-centos:3.1.1 .
```

### Building OAP Docker Image including Gazelle and other OAP projects
Building a Docker image with OAP v1.2 packages.
```
docker build --tag oap-centos:1.2.0 .
```

### Building your own Docker Image with the target applications
Building a Docker image with your own 



## Run Spark on Kubernetes
Before doing this, we assume you have setup Kubernetes enironment and it worked properly. All the tool scripts are under "spark" folder.
We tested these scripts in Minikube environment. If you are using other Kubernetes distributions, you may need to make some changes to work properly.

### Create Spark User and Assign Cluster Role
Spark running on Kubernetes needs edit role of your Kubernetes cluster to create driver or executor pods.
Go to spark folder and execute the following command to create "spark" user and assign the role. Make sure you have logged in Kubernetes and have administor role of the cluster.
```
sh ./spark-kubernetes-prepare.sh
```

### Run Spark/OAP Job in Cluster mode
In Kubernetes, you can run Spark/OAP job using spark-submit in Cluster mode at any node which has access to your Kubernetes API server.

You can edit spark configuration files in spark/conf directory

#### Run Spark Pi Job
You can run a Spark Pi job for a simple testing of the enironment is working. Execute the following command. If you are running on the master node,  you can ignore the --master parameter.
For example:
```
sh ./spark-pi.sh --master localhost:8443  --image oap-centos:1.1.1  --spark_conf ./conf
