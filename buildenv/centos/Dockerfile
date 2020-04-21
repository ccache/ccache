FROM centos:latest

# note: graphviz adds libX11... :'‑(
RUN yum install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm \
        && yum install -y \
                gcc \
                make \
                bash \
                build-essential \
                cmake \
                asciidoc \
                autoconf \
                libzstd-devel \
        && rpm -e --nodeps graphviz \
        && yum autoremove -y \
        && yum clean all
