# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Minimal Linux guest that runs FORMOSA applications against the device on a
# PCI bus (ilha/qemu.lua). The host's /nix/store and the repository are shared
# over 9p, so binaries built on the host run unchanged.
{ pkgs }:
let
  kernel = pkgs.linuxPackages.kernel;

  moduleClosure = pkgs.makeModulesClosure {
    kernel = kernel.modules;
    firmware = pkgs.emptyDirectory;
    rootModules = [
      "virtio_pci"
      "virtio_iommu"
      "9pnet_virtio"
      "9p"
      "vfio_iommu_type1"
      "vfio_pci"
    ];
  };

  # nixpkgs builds virtio and 9p as xz-compressed modules, which this kernel
  # cannot decompress itself; ship them uncompressed.
  modules = pkgs.runCommand "formosa-qemu-guest-modules" {
    nativeBuildInputs = [ pkgs.xz pkgs.kmod ];
  } ''
    cp -r --no-preserve=mode ${moduleClosure} $out
    find $out -name '*.ko.xz' -exec xz -d {} +
    depmod -b $out ${kernel.modDirVersion}
  '';

  # Runs a command in the host's environment, which qemu.lua writes to
  # /host/environ, with the guest's tools after the host's.
  hostEnv = pkgs.writeScript "formosa-qemu-guest-host-env" ''
    #!${pkgs.busybox}/bin/sh
    guest_path=$PATH
    . /host/environ
    export PATH="$PATH:$guest_path"
    exec "$@"
  '';

  # qemu.lua shares a run directory as /host: `env` names the shares and the
  # device, `environ` is the command's environment, and `cmd`, if present, is
  # the command to run before powering off.
  init = pkgs.writeScript "formosa-qemu-guest-init" ''
    #!${pkgs.busybox}/bin/sh
    # clinfo shows the OpenCL devices the guest's applications see.
    export PATH=${pkgs.lib.makeBinPath [ pkgs.busybox pkgs.kmod pkgs.clinfo ]}
    export HOME=/root TMPDIR=/tmp

    # Programs such as PoCL run their tools through /bin/sh, and wrappers and
    # scripts reach theirs through /usr/bin/env.
    mkdir -p /proc /sys /dev /tmp /root /bin /usr/bin
    ln -s ${pkgs.busybox}/bin/sh /bin/sh
    ln -s ${pkgs.busybox}/bin/env /usr/bin/env
    mount -t proc proc /proc
    mount -t sysfs sys /sys
    mount -t devtmpfs dev /dev

    modprobe -a virtio_pci virtio_iommu 9pnet_virtio 9p vfio_pci
    # x86's virtio-iommu has no interrupt remapping, which VFIO otherwise
    # requires; the PCI HAL polls and uses no interrupts.
    modprobe vfio_iommu_type1 allow_unsafe_interrupts=1

    share() {
      mkdir -p "$2"
      mount -t 9p -o "trans=virtio,version=9p2000.L,msize=1048576$3" "$1" "$2"
    }
    share host /host
    . /host/env
    share nixstore /nix/store ,ro
    share formosa "$FORMOSA_SOURCE"
    # After the repository, which may hold it.
    share tmp "$FORMOSA_TMPDIR"

    # The PCI HAL drives the device through VFIO.
    echo vfio-pci > "/sys/bus/pci/devices/$FORMOSA_PCI_DEVICE/driver_override"
    echo "$FORMOSA_PCI_DEVICE" > /sys/bus/pci/drivers_probe

    cd "$FORMOSA_CWD"
    if [ -f /host/cmd ]; then
      ${hostEnv} sh /host/cmd
      status=$?
      # isa-debug-exit at port 0xf4 makes QEMU exit with (value << 1) | 1,
      # which keeps 7 bits. The value is status + 1, so QEMU's own exit 1
      # never reads as a status; a signal's 128 + N would wrap.
      if [ "$status" -gt 126 ]; then
        echo "command exited with $status"
        status=126
      fi
      printf "\\$(printf %o $((status + 1)))" |
        dd of=/dev/port bs=1 seek=244 count=1 2>/dev/null
    else
      setsid cttyhack ${hostEnv} sh
    fi
    poweroff -f
  '';

  initrd = pkgs.makeInitrd {
    contents = [
      {
        object = init;
        symlink = "/init";
      }
      {
        object = "${modules}/lib";
        symlink = "/lib";
      }
    ];
  };
in pkgs.runCommand "formosa-qemu-guest" { } ''
  mkdir $out
  # Once the host's /nix/store covers the initrd's, the init runs from the
  # host's copy, so the guest keeps it, and busybox and kmod, in its closure.
  ln -s ${init} $out/init
  ln -s ${kernel}/bzImage $out/bzImage
  ln -s ${initrd}/initrd $out/initrd
''
