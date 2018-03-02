#!/bin/sh

ORIG=ROCK-Kernel-Driver-fkxamd-drm-next-wip
DIR=/tmp/source

rm -rf $DIR
mkdir -p $DIR/{include/{drm,uapi/{drm,linux}},radeon}

cp -a $ORIG/drivers/gpu/drm/amd $DIR/
cp -a $ORIG/drivers/gpu/drm/ttm $DIR/
cp -a $ORIG/include/drm/ttm $DIR/include/drm
cp -a $ORIG/include/drm/gpu_scheduler* $DIR/include/drm
cp -a $ORIG/include/uapi/drm/amdgpu_drm.h $DIR/include/uapi/drm
cp -a $ORIG/include/uapi/drm/drm.h $DIR/include/uapi/drm
cp -a $ORIG/include/uapi/drm/drm_mode.h $DIR/include/uapi/drm
#cp -a $ORIG/include/kcl $DIR/include
#cp -a $ORIG/include/drm/amd_rdma.h $DIR/include/drm
cp -a $ORIG/include/uapi/linux/kfd_ioctl.h $DIR/include/uapi/linux
cp -a $ORIG/drivers/gpu/drm/radeon/cik_reg.h $DIR/radeon/

cp -a source.orig/* $DIR/
mv $DIR/Makefile.amdgpu $DIR/amd/amdgpu/Makefile
mv $DIR/Makefile.amkfd $DIR/amd/amdkfd/Makefile
mv $DIR/Makefile.ttm $DIR/ttm/Makefile

if [ "$1" = "compile" ]; then
	rm -rf /usr/src/amdgpu-17.50-511655.el7/
	mkdir -p /usr/src/amdgpu-17.50-511655.el7/
	cp -a $DIR/* /usr/src/amdgpu-17.50-511655.el7/
	/usr/lib/dkms/common.postinst amdgpu 17.50-511655.el7
else
	pushd $DIR
	make -C /lib/modules/4.15.6-berry/build M=`pwd`
	popd
fi

