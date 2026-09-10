FROM --platform=linux/amd64 ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Seoul

RUN apt-get update && apt-get install -y \
    build-essential \
    gcc-multilib \
    g++-multilib \
    qemu-system-x86 \
    gdb \
    git \
    make \
    bash-completion \
    vim \
    nano \
    tzdata \
    && rm -rf /var/lib/apt/lists/*

ENV PATH="/pintos/src/utils:${PATH}"

RUN echo 'export PS1="\[\033[01;32m\]\u@\h\[\033[00m\]:\[\033[01;34m\]\w\[\033[00m\]\$ "' >> /root/.bashrc && \
    echo 'alias ls="ls --color=auto"' >> /root/.bashrc && \
    echo 'alias ll="ls -alF --color=auto"' >> /root/.bashrc

WORKDIR /pintos

CMD ["bash"]