FROM centos:latest

# note: graphviz adds libX11... :'‑(
RUN yum install -y \
                gcc \
                make \
                bash \
                asciidoc \
                autoconf \
                gperf \
                zlib-devel \
        && rpm -e --nodeps graphviz \
        && yum autoremove -y \
        && yum clean all
