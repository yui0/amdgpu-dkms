# amdgpu-dkms

amdgpu-pro driver in DKMS format.

version: amdgpu-17.50-511655.el7
kernel: 4.15.6

## How to use

```bash
# cp -a amdgpu-dkms /usr/src/
# dnf install dkms
# /usr/lib/dkms/common.postinst amdgpu 17.50-511655.el7
```

dkms uninstall -m amdgpu -v 17.50-511655.el7

```bash
# sh build.sh `uname -r`
# make -C /lib/modules/`uname -r`/build M=`pwd`
# make -C /lib/modules/`uname -r`/build M=`pwd` modules_install
```

rm /lib/modules/`uname -r`/extra

