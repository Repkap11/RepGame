ARG ubuntu_version
FROM ubuntu:$ubuntu_version

ARG repgame_packages

RUN apt-get update ;\
    apt-get install -y --no-install-recommends git make sudo pciutils coreutils ca-certificates lsb-release patch fuse file ;\
    apt-get install -y --no-install-recommends $repgame_packages ;\
    rm -rf /var/lib/apt/lists/*

ARG user_name
ARG user_id
RUN adduser --disabled-password --gecos "" --uid $user_id $user_name ;\
    adduser $user_name sudo ;\
    echo "$user_name ALL=(root) NOPASSWD:ALL" > /etc/sudoers.d/user && chmod 0440 /etc/sudoers.d/user
USER $user_name
WORKDIR /home/$user_name/RepGame

ENV NVIDIA_DRIVER_CAPABILITIES graphics,utility