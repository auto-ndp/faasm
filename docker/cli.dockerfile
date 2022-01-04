ARG FAASM_VERSION
FROM kubasz51/faasm-base:$FAASM_VERSION

SHELL ["/bin/bash", "-c"]

# Install various deps
RUN apt-get update
RUN apt-get install --no-install-recommends --no-install-suggests -y \
    clang-tidy-11 \
    clang-tidy-13 \
    libpython3-dev \
    python3-dev \
    python3-pip \
    python3-venv \
    libcairo2-dev \
    python3-cairo \
    vim

# Install wabt
# TODO - pin this to a release
RUN git clone https://github.com/WebAssembly/wabt/ /tmp/wabt
WORKDIR /tmp/wabt/build
RUN cmake -GNinja -DBUILD_TESTS=OFF -DBUILD_LIBWASM=OFF ..
RUN ninja install
WORKDIR /
RUN rm -r /tmp/wabt

# Python set-up
WORKDIR /usr/local/code/faasm
RUN ./bin/create_venv.sh

# Build some useful targets
RUN source venv/bin/activate && inv -r faasmcli/faasmcli dev.tools --build Release

# Remove worker entrypoint
COPY bin/noop-entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]

ENV TERM xterm-256color

# Prepare bashrc
RUN echo ". /usr/local/code/faasm/bin/workon.sh" >> ~/.bashrc
CMD ["/bin/bash", "-l"]

