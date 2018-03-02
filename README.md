# amdgpu-dkms

amdgpu-pro driver in DKMS format.

## How to use

```bash
# make -C /lib/modules/`uname -r`/build M=`pwd`
# make -C /lib/modules/`uname -r`/build M=`pwd` modules_install
```

rm /lib/modules/`uname -r`/extra
/usr/lib/dkms/common.postinst amdgpu 17.50-511655.el7

