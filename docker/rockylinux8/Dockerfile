FROM rockylinux:8.6
RUN dnf install -y 'dnf-command(config-manager)'
RUN dnf config-manager --set-enabled powertools
RUN dnf install -y epel-release
RUN dnf check-update || true
RUN dnf upgrade -y
RUN dnf install -y python3 python3-pip python3-devel python3-jsonschema python3-pyyaml python3-pybind11 python3-wheel gdb vim git gcc gcc-c++ make cmake libuuid-devel json-c-devel hwloc-devel tbb-devel cli11-devel spdlog-devel libedit-devel systemd-devel doxygen python3-sphinx pandoc rpm-build rpmdevtools python3-virtualenv yaml-cpp-devel libudev-devel libcap-devel sudo numactl-devel
RUN git config --system safe.directory '*'
WORKDIR /root

COPY scripts/test-rpms.sh /scripts/test-rpms.sh

ENTRYPOINT ["/opae-rockylinux8/opae-sdk/packaging/opae/rpm/create", "unrestricted"]
