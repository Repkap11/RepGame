ARG ubuntu_version
FROM ubuntu:$ubuntu_version

ARG repgame_packages
ARG sdl3_version=3.4.16

# Install base build tools and SDL3 build dependencies.
# SDL3 is built from source because Ubuntu 24.04 doesn't ship libsdl3-dev.
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        git make sudo pciutils coreutils ca-certificates lsb-release patch fuse file \
        pkg-config cmake wget gnupg \
        libx11-dev libxext-dev libxrandr-dev libxss-dev libxtst-dev libxi-dev \
        libxcursor-dev libxinerama-dev libxrender-dev libxfixes-dev \
        libwayland-dev wayland-protocols libxkbcommon-dev libdbus-1-dev \
        libgl-dev libegl-dev libgles-dev libglu1-mesa-dev \
        libasound2-dev libpulse-dev libdecor-0-dev && \
    rm -rf /var/lib/apt/lists/*

# Add AMD ROCm repository for HIP 7.x (24.04 default repos only have 5.7.1).
# Install rocm-hip-runtime-dev which provides libamdhip64.so.7 and hipcc.
# update-alternatives links hipcc to /usr/bin/hipcc automatically.
RUN mkdir --parents --mode=0755 /etc/apt/keyrings && \
    wget -q https://repo.radeon.com/rocm/rocm.gpg.key -O - | gpg --dearmor | tee /etc/apt/keyrings/rocm.gpg > /dev/null && \
    echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/rocm.gpg] https://repo.radeon.com/rocm/apt/7.2.4 noble main" > /etc/apt/sources.list.d/rocm.list && \
    echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/rocm.gpg] https://repo.radeon.com/graphics/7.2.4/ubuntu noble main" >> /etc/apt/sources.list.d/rocm.list && \
    printf "Package: *\nPin: release o=repo.radeon.com\nPin-Priority: 600\n" > /etc/apt/preferences.d/rocm-pin-600 && \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends rocm-hip-runtime-dev && \
    echo "/opt/rocm/lib" > /etc/ld.so.conf.d/rocm.conf && ldconfig && \
    rm -rf /var/lib/apt/lists/*

# Install repgame packages (hipcc and libamdhip64-dev are handled by ROCm above).
# Use && so failures actually fail the build.
RUN apt-get update && \
    ln -fs /usr/share/zoneinfo/America/New_York /etc/localtime && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $repgame_packages && \
    rm -rf /var/lib/apt/lists/*

# Build SDL3 from source.
# Vulkan is disabled to avoid pulling in extra GPU deps; the game uses OpenGL.
RUN wget -q https://github.com/libsdl-org/SDL/releases/download/release-${sdl3_version}/SDL3-${sdl3_version}.tar.gz && \
    tar xzf SDL3-${sdl3_version}.tar.gz && \
    cd SDL3-${sdl3_version} && \
    cmake -B build -DCMAKE_INSTALL_PREFIX=/usr \
        -DSDL_STATIC=OFF -DSDL_SHARED=ON \
        -DSDL_TEST=OFF \
        -DSDL_OPENGL=ON -DSDL_OPENGLES=ON \
        -DSDL_VULKAN=OFF && \
    cmake --build build -j$(nproc) && \
    cmake --install build && \
    ldconfig && \
    cd .. && rm -rf SDL3-${sdl3_version} SDL3-${sdl3_version}.tar.gz

ARG user_name
ARG user_id
RUN if id -u $user_id >/dev/null 2>&1; then userdel -r $(getent passwd $user_id | cut -d: -f1) 2>/dev/null || true; fi && \
    adduser --disabled-password --gecos "" --uid $user_id $user_name && \
    adduser $user_name sudo && \
    echo "$user_name ALL=(root) NOPASSWD:ALL" > /etc/sudoers.d/user && chmod 0440 /etc/sudoers.d/user
USER $user_name
WORKDIR /home/$user_name/RepGame

ENV NVIDIA_DRIVER_CAPABILITIES graphics,utility
ENV PATH=/opt/rocm/bin:$PATH
ENV LIBRARY_PATH=/opt/rocm/lib:$LIBRARY_PATH
