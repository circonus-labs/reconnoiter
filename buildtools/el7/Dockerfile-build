#
# Reconnoiter build container
#
# Contains a copy of the source tree + build artifacts
#

FROM reconnoiter-el7-base

RUN sudo yum -y update

ADD . /reconnoiter
RUN sudo chown noit:noit -R /reconnoiter

RUN /reconnoiter-base/cmd.sh ./buildtools/local-rebuild.sh el7

CMD ["bash"]
