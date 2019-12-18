ARG ubuntu_version
FROM ubuntu:$ubuntu_version

RUN apt-get update
RUN apt-get install -y --no-install-recommends git make sudo pciutils coreutils ca-certificates lsb-release patch fuse file

ARG repgame_packages
RUN apt-get install -y --no-install-recommends $repgame_packages

ENV NVIDIA_DRIVER_CAPABILITIES ${NVIDIA_DRIVER_CAPABILITIES:+$NVIDIA_DRIVER_CAPABILITIES,}graphics,utility

ARG user_name
ARG user_id
RUN adduser --disabled-password --gecos "" --uid $user_id $user_name
RUN adduser $user_name sudo
RUN echo "$user_name ALL=(root) NOPASSWD:ALL" > /etc/sudoers.d/user && chmod 0440 /etc/sudoers.d/user
USER $user_name
WORKDIR /home/$user_name/RepGame