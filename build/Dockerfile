# had to use ubuntu. alpine uses musl instead of glibc,
# slc's python lib uses some glibc symbols not available in gcompat
FROM ubuntu:noble

RUN apt update && DEBIAN_FRONTEND="noninteractive" apt install -y git openjdk-17-jre-headless bash busybox && \
    mkdir /tools/

ARG repo=https://github.com/SiliconLabs/gecko_sdk.git
ARG ref=gsdk_4.0

RUN git clone "${repo}" -b "${ref}" /tools/gsdk

ARG zap_zip=https://github.com/project-chip/zap/releases/download/v2024.05.08/zap-linux-x64.zip

RUN mkdir /tmp/extracted && \
    cd /tools/ && \
    # Piping wget to unzip seems to break the permissions for the extracted files
    busybox wget "https://www.silabs.com/documents/login/software/slc_cli_linux.zip" -O /tmp/slc_cli_linux.zip && \
    busybox unzip -d /tmp/extracted/ /tmp/slc_cli_linux.zip && \
    rm /tmp/slc_cli_linux.zip && \
    for single_dir in /tmp/extracted/*; do \
        mv "$single_dir" "/tools/slc-cli"; \
    done

RUN cd /tools/ && \
    busybox wget "${zap_zip}" -O "/tmp/zap.zip" && \
    busybox unzip -d /tmp/extracted/ "/tmp/zap.zip" && \
    rm "/tmp/zap.zip" && \
    mv /tmp/extracted/ /tools/zap/

# STUDIO_ADAPTER_PACK_PATH=/tools/zap/ /tools/slc-cli/slc generate -np --sdk /tools/gsdk/ /tools/gsdk/protocol/zigbee/app/framework/scenarios/z3/ZigbeeMinimalHost/ZigbeeMinimalHost.slcp -cp -d /src/