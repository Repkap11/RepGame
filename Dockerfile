FROM ubuntu:18.04

RUN apt-get update && apt-get upgrade -y
RUN apt-get install -y --no-install-recommends git make sudo pciutils coreutils ca-certificates

ARG repgame_packages
RUN apt-get install -y --no-install-recommends $repgame_packages

ARG user_name
RUN useradd --create-home --shell /bin/bash $user_name
RUN adduser $user_name sudo
RUN echo "$user_name ALL=(root) NOPASSWD:ALL" > /etc/sudoers.d/user && chmod 0440 /etc/sudoers.d/user
USER $user_name
WORKDIR /home/$user_name/RepGame

CMD make install && make linux windows
#CMD bash