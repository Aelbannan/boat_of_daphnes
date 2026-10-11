FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG CMAKE_VERSION=3.31.10
ARG APPIMAGETOOL_VERSION=1.9.1
ARG APPIMAGETOOL_X86_64_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
ARG TYPE2_RUNTIME_RELEASE=20251108
ARG TYPE2_RUNTIME_X86_64_SHA256=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d

# This is the shared Linux build environment for GitHub Actions and
# scripts/linux/build.py --ci-container. Keep tool and library dependencies
# here rather than duplicating them in the workflow.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates wget python3 python3-pip python3-venv \
        build-essential git clang-18 lld-18 llvm-18 libclang-rt-18-dev \
        libc++-18-dev libc++abi-18-dev ninja-build pkg-config binutils \
        patchelf zsync imagemagick file desktop-file-utils \
        libwayland-dev libxkbcommon-dev libx11-xcb-dev libx11-dev \
        libxrandr-dev libxext-dev libxi-dev libxcursor-dev libgl1-mesa-dev \
        libvulkan-dev libasound2-dev libpulse-dev libxss-dev libxtst-dev \
        zlib1g-dev libpng-dev libfreetype6-dev libsqlite3-dev libzstd-dev \
        libfmt-dev libbz2-dev libbrotli-dev libicu-dev \
    && python3 -m venv /opt/cmake \
    && /opt/cmake/bin/pip install --no-cache-dir "cmake==${CMAKE_VERSION}" \
    && rm -rf /var/lib/apt/lists/*

# Do not install Ubuntu's libabsl-dev. CI explicitly disables system Abseil,
# while the unchanged playable builder's Dawn package selects Aurora's pinned,
# compatible fallback when no system package is present.

# Pin both AppImage executables and verify them before they enter the image.
# Update a version and checksum together from its immutable release page:
# https://github.com/AppImage/appimagetool/releases/tag/1.9.1
# https://github.com/AppImage/type2-runtime/releases/tag/20251108
RUN mkdir -p /opt/appimage \
    && wget -q \
        "https://github.com/AppImage/appimagetool/releases/download/${APPIMAGETOOL_VERSION}/appimagetool-x86_64.AppImage" \
        -O /opt/appimage/appimagetool-x86_64.AppImage \
    && printf '%s  %s\n' "${APPIMAGETOOL_X86_64_SHA256}" /opt/appimage/appimagetool-x86_64.AppImage \
        | sha256sum --check --strict - \
    && wget -q \
        "https://github.com/AppImage/type2-runtime/releases/download/${TYPE2_RUNTIME_RELEASE}/runtime-x86_64" \
        -O /opt/appimage/runtime-x86_64 \
    && printf '%s  %s\n' "${TYPE2_RUNTIME_X86_64_SHA256}" /opt/appimage/runtime-x86_64 \
        | sha256sum --check --strict - \
    && chmod 0755 /opt/appimage/appimagetool-x86_64.AppImage /opt/appimage/runtime-x86_64

ENV PATH="/opt/cmake/bin:/usr/lib/llvm-18/bin:${PATH}" \
    APPIMAGETOOL=/opt/appimage/appimagetool-x86_64.AppImage \
    APPIMAGE_RUNTIME_FILE=/opt/appimage/runtime-x86_64 \
    APPIMAGE_EXTRACT_AND_RUN=1
