#
# Baseimage containing snowth build environment on centos7
#

# centos7 base image
FROM centos:7
RUN yum -y update && yum clean all
RUN yum -y install sudo git

# Add Circonus repository
ADD ./buildtools/el7/el7-Circonus.repo /etc/yum.repos.d/Circonus.repo
RUN yum -y update

# Keep install dependencies in a separate layer so that rebuilds don't download all everything again
RUN mkdir -p /reconnoiter-base/
ADD ./buildtools/el7/install-dependencies.sh /reconnoiter-base
ADD ./buildtools/el7/env.inc /reconnoiter-base
ADD ./buildtools/el7/cmd.sh /reconnoiter-base

RUN /reconnoiter-base/install-dependencies.sh

# Prepare work environment
RUN mkdir /reconnoiter

# Create a user with sudo rights
RUN useradd -ms /bin/bash noit
RUN chown noit:noit -R /reconnoiter
RUN printf "noit\tALL=(ALL)\tNOPASSWD: ALL" > /etc/sudoers.d/noit

WORKDIR /reconnoiter
USER noit
