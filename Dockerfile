FROM alpine:3.20
RUN apk update
RUN apk add clang lld git make llvm

RUN apk add bash build-base cmake python3

# Установим wasi-libc в /opt/wasi-libc
ENV INSTALL_DIR=/opt/wasi-libc

# Клонируем и собираем wasi-libc
RUN git clone https://github.com/WebAssembly/wasi-libc.git 
RUN cd wasi-libc && ls && \
    make install INSTALL_DIR=/opt/wasi-libc && \
    rm -rf wasi-libc

RUN mkdir -p /usr/lib/llvm17/lib/clang/17/lib/wasi/ && \
    wget -O /usr/lib/llvm17/lib/clang/17/lib/wasi/libclang_rt.builtins-wasm32.a https://github.com/jedisct1/libclang_rt.builtins-wasm32.a/blob/master/precompiled/llvm-17/libclang_rt.builtins-wasm32.a?raw=true

RUN mkdir /src && chown nobody /src
USER nobody
COPY --chown=nobody:nobody /src/ /src/
WORKDIR /src
# RUN clang --target=wasm32-unknown-wasi --sysroot /opt/wasi-libc -nostartfiles -Wl,--no-entry -Wl,--export-all -o /tmp/chip.wasm /src/main.c
RUN clang --target=wasm32-unknown-wasi --sysroot /opt/wasi-libc -nostartfiles -Wl,--import-memory -Wl,--export-table  \
    -Wl,--no-entry -Werror -o /tmp/chip.wasm /src/main.c
ENV HEXI_SRC_DIR="/src"
ENV HEXI_BUILD_CMD="clang --target=wasm32-unknown-wasi --sysroot /opt/wasi-libc -nostartfiles -Wl,--export-table -Wl,--no-entry -Werror -o /tmp/chip.wasm /src/main.c"
ENV HEXI_OUT_HEX="/tmp/chip.wasm"
ENV HEXI_OUT_ELF="/tmp/chip.elf"

