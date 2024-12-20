#!/bin/bash
set -e

#Setup common variables
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabi-
if [ -n "`echo ${LICHEE_CHIP} | grep "sun5[0-9]i"`" ] && \
        [ "x${LICHEE_ARCH}" = "xarm64" ]; then
    export ARCH=arm64
    export CROSS_COMPILE=aarch64-linux-gnu-
fi

if [ -n "${LICHEE_TOOLCHAIN_PATH}" \
        -a -d "${LICHEE_TOOLCHAIN_PATH}" ]; then
    GCC=$(find ${LICHEE_TOOLCHAIN_PATH} -perm /a+x -a -regex '.*-gcc');
    export CROSS_COMPILE="${GCC%-*}-";
elif [ -n "${LICHEE_CROSS_COMPILER}" ]; then
    export CROSS_COMPILE="${LICHEE_CROSS_COMPILER}-"
fi

export AS=${CROSS_COMPILE}as
export LD=${CROSS_COMPILE}ld
export CC=${CROSS_COMPILE}gcc
export AR=${CROSS_COMPILE}ar
export NM=${CROSS_COMPILE}nm
export STRIP=${CROSS_COMPILE}strip
export OBJCOPY=${CROSS_COMPILE}objcopy
export OBJDUMP=${CROSS_COMPILE}objdump
export LOCALVERSION=""
MKBOOTIMG=${LICHEE_TOOLS_DIR}/pack/pctools/linux/android/mkbootimg
if [ -f ${MKBOOTIMG} ]; then
	export MKBOOTIMG
else
	export MKBOOTIMG=${KUNOS_TOOLS_DIR}/android/mkbootimg
fi
if [ "x$CCACHE_DIR" != "x" ];then
	CCACHE_Y="ccache "
fi

KERNEL_VERSION=`make -s kernelversion -C ./`
LICHEE_KDIR=`pwd`
LICHEE_MOD_DIR=${LICHEE_KDIR}/output/lib/modules/${KERNEL_VERSION}
export LICHEE_KDIR

update_kern_ver()
{
    if [ -r include/generated/utsrelease.h ]; then
        KERNEL_VERSION=`cat include/generated/utsrelease.h |awk -F\" '{print $2}'`
    fi
    LICHEE_MOD_DIR=${LICHEE_KDIR}/output/lib/modules/${KERNEL_VERSION}
}

show_help()
{
    printf "
    Build script for Lichee platform

    Invalid Options:

    help         - show this help
    kernel       - build kernel
    modules      - build kernel module in modules dir
    clean        - clean kernel and modules

    "
}

build_nand()
{
    NAND_ROOT=${LICHEE_KDIR}/modules/nand

    make -C modules/nand LICHEE_MOD_DIR=${LICHEE_MOD_DIR} \
        LICHEE_KDIR=${LICHEE_KDIR} \
        CONFIG_CHIP_ID=${CONFIG_CHIP_ID} install

}

gpu_message()
{
    echo -ne "\033[34;1m"
    echo "[GPU]: $1"
    echo -ne "\033[0m"
}

build_gpu()
{
    GPU_TYPE=`fgrep CONFIG_SUNXI_GPU_TYPE ${LICHEE_KDIR}/.config | cut -d \" -f 2`
    if [ "X$GPU_TYPE" = "XNone" -o "X$GPU_TYPE" = "X" ]; then
    {
        gpu_message "No GPU type is configured in ${LICHEE_KDIR}/.config."
        return
    }
    fi

    gpu_message "Building $GPU_TYPE device driver..."

    if [ "X${LICHEE_PLATFORM}" = "Xandroid" -o "X${LICHEE_PLATFORM}" = "Xsecureandroid" ] ; then
    {
        TMP_OUT=$OUT
        TMP_TOP=$TOP
        unset OUT
        unset TOP
    }
    fi

    make -C modules/gpu LICHEE_MOD_DIR=${LICHEE_MOD_DIR} LICHEE_KDIR=${LICHEE_KDIR}

    if [ "X${LICHEE_PLATFORM}" = "Xandroid" -o "X${LICHEE_PLATFORM}" = "Xsecureandroid" ] ; then
    {
        export OUT=$TMP_OUT
        export TOP=$TMP_TOP
    }
    fi

    gpu_message "$GPU_TYPE device driver has been built."
}

clean_gpu()
{
    GPU_TYPE=`fgrep CONFIG_SUNXI_GPU_TYPE ${LICHEE_KDIR}/.config | cut -d \" -f 2`
    if [ "X$GPU_TYPE" = "XNone" -o "X$GPU_TYPE" = "X" ]; then
    {
        gpu_message "No GPU type is configured in .config."
        return
    }
    fi

    gpu_message "Cleaning $GPU_TYPE device driver..."
    make -C modules/gpu LICHEE_MOD_DIR=${LICHEE_MOD_DIR} LICHEE_KDIR=${LICHEE_KDIR} clean
    gpu_message "$GPU_TYPE device driver has been cleaned."
}

build_check()
{
    if [ "x$SUNXI_CHECK" = "x1" ];then
        SUNXI_SPARSE_CHECK=1
        SUNXI_SMATCH_CHECK=1
        SUNXI_STACK_CHECK=1
    fi

    checktools_dir=$LICHEE_TOOLS_DIR/codecheck
    tools_sparse=$checktools_dir/sparse/sparse
    tools_smatch=$checktools_dir/smatch/smatch

    if [ "x$SUNXI_SPARSE_CHECK" = "x1" ] && [ -f $tools_sparse ];then
        echo "\n\033[0;31;1mBuilding Round for sparse check...\033[0m\n\n"
        CHECK="$tools_sparse"
        make CHECK=${CHECK} ARCH=${ARCH} CROSS_COMPILE="${CCACHE_Y}${CROSS_COMPILE}" -j${LICHEE_JLEVEL} \
                C=2 all modules 2>&1|tee output/build_sparse.txt
        cat output/build_sparse.txt|egrep -w '(warn|error|warning)' >output/warn_sparse.txt
    fi

    if [ "x$SUNXI_SMATCH_CHECK" = "x1" ]&& [ -f $tools_smatch ];then
        echo "\n\033[0;31;1mBuilding Round for smatch check...\033[0m\n\n"
        CHECK="$tools_smatch --full-path --no-data -p=kkernel"
        make CHECK=${CHECK} ARCH=${ARCH} CROSS_COMPILE="${CCACHE_Y}${CROSS_COMPILE}" -j${LICHEE_JLEVEL} \
                C=2 all modules 2>&1|tee output/build_smatch.txt
        cat output/build_smatch.txt|egrep -w '(warn|error|warning)' >output/warn_smatch.txt
    fi

    if [ "x$SUNXI_STACK_CHECK" = "x1" ];then
        make ARCH=${ARCH} CROSS_COMPILE="${CCACHE_Y}${CROSS_COMPILE}" -j${LICHEE_JLEVEL} checkstack 2>&1 \
                |tee output/warn_stack.txt
    fi
}

build_dts(){
    if [ ! -f .config ] ; then
        printf "\n\033[0;31;1mUsing default config ${LICHEE_KERN_DEFCONF_ABSOLUTE} ...\033[0m\n\n"
        make ARCH=${ARCH} defconfig KBUILD_DEFCONFIG=${LICHEE_KERN_DEFCONF_RELATIVE}
    fi

    echo "---build dts for ${LICHEE_CHIP} ${LICHEE_BOARD}-----"
    if [ "x${ARCH}" = "xarm" ];then
        dts_path="arch/arm/boot/dts"
    else
        dts_path="arch/arm64/boot/dts/sunxi"
	prefix="sunxi/"
    fi
    dtb_file=board.dtb
    dts_file=board.dts
    if [ ! -f $dts_path/$dts_file ];then
	dtb_file="${LICHEE_CHIP}-${LICHEE_BOARD}.dtb"
    	dts_file="${LICHEE_CHIP}-${LICHEE_BOARD}.dts"
    fi
    if [ ! -f $dts_path/$dts_file ];then
        dts_file="${LICHEE_CHIP}-soc.dts"
        dtb_file="${LICHEE_CHIP}-soc.dtb"
    fi
    make ARCH=${ARCH} ${prefix}${dtb_file}

    if [ "x${ARCH}" = "xarm64" ]; then
        if [ -f arch/${ARCH}/boot/dts/sunxi/board.dtb ]; then
            cp arch/${ARCH}/boot/dts/sunxi/board.dtb output/sunxi.dtb
        elif [ -f arch/${ARCH}/boot/dts/sunxi/${LICHEE_CHIP}-${LICHEE_BOARD}.dtb ]; then
            cp arch/${ARCH}/boot/dts/sunxi/${LICHEE_CHIP}-${LICHEE_BOARD}.dtb output/sunxi.dtb
        elif [ -f arch/${ARCH}/boot/dts/sunxi/${LICHEE_CHIP}-soc.dtb ]; then
            cp arch/${ARCH}/boot/dts/sunxi/${LICHEE_CHIP}-soc.dtb output/sunxi.dtb
        else
            echo "sunxi.dtb" > output/sunxi.dtb
       fi
    else
        if [ -f arch/${ARCH}/boot/dts/board.dtb ]; then
            cp arch/${ARCH}/boot/dts/board.dtb output/sunxi.dtb
        elif [ -f arch/${ARCH}/boot/dts/${LICHEE_CHIP}-${LICHEE_BOARD}.dtb ]; then
            cp arch/${ARCH}/boot/dts/${LICHEE_CHIP}-${LICHEE_BOARD}.dtb output/sunxi.dtb
        elif [ -f arch/${ARCH}/boot/dts/${LICHEE_CHIP}-soc.dtb ]; then
            cp arch/${ARCH}/boot/dts/${LICHEE_CHIP}-soc.dtb output/sunxi.dtb
        else
            echo "sunxi.dtb" > output/sunxi.dtb
        fi
    fi

    # It's used for dtb debug
    ./scripts/dtc/dtc -I dtb -O dts -o output/.sunxi.dts output/sunxi.dtb -W no-unit_address_vs_reg
}

build_kernel()
{
    echo "Building kernel.."

    cd ${LICHEE_KDIR}
    rm -rf output/
    echo "${LICHEE_MOD_DIR}"
    mkdir -p ${LICHEE_MOD_DIR}

    # uImage is arm architecture specific target
    local arch_target=""
    if [ "${ARCH}" = "arm" ]; then
        arch_target="uImage dtbs"
    else
        arch_target="all"
    fi

	#exchange sdc0 and sdc2 for dragonBoard card boot
	if [ "x${LICHEE_PLATFORM}" = "xdragonboard" -o "x${LICHEE_PLATFORM}" = "xdragonmat" ]; then
		local SYS_CONFIG_FILE=../tools/pack/chips/${LICHEE_CHIP}/configs/${LICHEE_BOARD}/sys_config.fex
		local DTS_PATH=./arch/${ARCH}/boot/dts/

		if [ -f ${DTS_PATH}/${LICHEE_CHIP}_bak.dtsi ];then
			rm -f ${DTS_PATH}/${LICHEE_CHIP}.dtsi
			mv ${DTS_PATH}/${LICHEE_CHIP}_bak.dtsi ${DTS_PATH}/${LICHEE_CHIP}.dtsi
		fi
		# if find dragonboard_test=1 in sys_config.fex ,then will exchange sdc0 and sdc2
		if [ -n "`grep "dragonboard_test" $SYS_CONFIG_FILE | grep "1" | grep -v ";"`" ]; then
			echo "exchange sdc0 and sdc2 for dragonboard card boot"
			./scripts/exchange-sdc0-sdc2-for-dragonboard.sh  ${LICHEE_CHIP}
		fi
	fi

    if [ ! -f .config ] ; then
        printf "\n\033[0;31;1mUsing default config ${LICHEE_KERN_DEFCONF_ABSOLUTE} ...\033[0m\n\n"
        make ARCH=${ARCH} defconfig KBUILD_DEFCONFIG=${LICHEE_KERN_DEFCONF_RELATIVE}
    fi

    if [ "x$PACK_TINY_ANDROID" = "xtrue" ]; then
	    ARCH=${ARCH} scripts/kconfig/merge_config.sh .config \
            linaro/configs/sunxi-tinyandroid.conf
    fi
    if [ "x$PACK_AUTOTEST" != "x" ]; then
	    if [ -f linaro/configs/sunxi-sata.conf ];then
	        ARCH=${ARCH} scripts/kconfig/merge_config.sh .config \
                linaro/configs/sunxi-common.conf
	    fi
    fi

    if [ "x$PACK_BSPTEST" != "x" -o "x$BUILD_SATA" != "x" -o "x$LICHEE_LINUX_DEV" = "xsata" ]; then
	    if [ -f linaro/configs/sunxi-common.conf ];then
	        ARCH=${ARCH} scripts/kconfig/merge_config.sh .config \
                linaro/configs/sunxi-common.conf
	    fi
	    if [ -f linaro/configs/sunxi-sata.conf ];then
	        ARCH=${ARCH} scripts/kconfig/merge_config.sh .config \
                linaro/configs/sunxi-sata.conf
	    fi
	    if [ -f linaro/configs/sunxi-sata-${LICHEE_CHIP}.conf ];then
	        ARCH=${ARCH} scripts/kconfig/merge_config.sh .config \
                linaro/configs/sunxi-sata-${LICHEE_CHIP}.conf
	    fi
	    if [ -f linaro/configs/sunxi-sata-${ARCH}.conf ];then
	        ARCH=${ARCH} scripts/kconfig/merge_config.sh .config \
                linaro/configs/sunxi-sata-${ARCH}.conf
	    fi
    fi
    bear make ARCH=${ARCH} CROSS_COMPILE="${CCACHE_Y}${CROSS_COMPILE}" -j${LICHEE_JLEVEL} ${arch_target} modules
    build_check
    update_kern_ver

    #The Image is origin binary from vmlinux.
    if [ -f arch/${ARCH}/boot/Image ]; then
        cp -vf arch/${ARCH}/boot/Image output/bImage
    fi

    if [ -f arch/${ARCH}/boot/zImage ] || [ -f arch/${ARCH}/boot/uImage ]; then
        cp -vf arch/${ARCH}/boot/[zu]Image output/
    fi

    if [ -f arch/${ARCH}/boot/Image.gz ]; then
        cp -vf arch/${ARCH}/boot/Image.gz output/
    fi

    echo "Copy rootfs.cpio.gz for ${ARCH}"
    if [ "${ARCH}" = "arm" ]; then
        cp -f rootfs_32bit.cpio.gz output/rootfs.cpio.gz
    else
        cp -f rootfs.cpio.gz output/rootfs.cpio.gz
    fi

    #1. longan, if it's self ramdisk is exist, use it
    #2. longan, if it's self ramdisk is not exist, use default ramdisk
    #3. else, use default ramdisk
    local longan_ramdisk="${LICHEE_PLAT_OUT}/ramfs/images/rootfs.cpio.gz"
    if [ "x${LICHEE_LINUX_DEV}" = "xlongan" -a -f ${longan_ramdisk} ]; then
        echo "Use longan ramdisk ...."
        cp -f ${longan_ramdisk} output/rootfs.cpio.gz
    fi

    cp .config output/

    tar -jcf output/vmlinux.tar.bz2 vmlinux
    for file in $(find drivers sound crypto block fs security net -name "*.ko"); do
        cp $file ${LICHEE_MOD_DIR}
    done
    ko_file=`find ${LICHEE_MOD_DIR} -name *.ko`

    if [ ! -z "$ko_file" ]; then
            ${STRIP} -d ${LICHEE_MOD_DIR}/*.ko
    fi
    cp -f Module.symvers ${LICHEE_MOD_DIR}

}

build_modules()
{

    echo "Building modules"

    if [ ! -f include/generated/utsrelease.h ]; then
        printf "Please build kernel first!\n"
        exit 1
    fi

    update_kern_ver
    if [ "${LICHEE_CHIP}" != "sun8iw8p1" ]; then
	build_gpu
    fi
}

regen_rootfs_cpio()
{
    echo "regenerate rootfs cpio"

    cd ${LICHEE_KDIR}/output
    if [ -x "../scripts/build_rootfs.sh" ]; then
        ../scripts/build_rootfs.sh e ./rootfs.cpio.gz > /dev/null
    else
        echo "No such file: scripts/build_rootfs.sh"
        exit 1
    fi

    mkdir -p ./skel/lib/modules/${KERNEL_VERSION}

    if [ -e ${LICHEE_MOD_DIR}/nand.ko ]; then
        cp ${LICHEE_MOD_DIR}/nand.ko ./skel/lib/modules/${KERNEL_VERSION}
        if [ $? -ne 0 ]; then
            echo "copy nand module error: $?"
            exit 1
        fi
    fi

    ko_file=`find ./skel/lib/modules/$KERNEL_VERSION/ -name *.ko`
    if [ ! -z "$ko_file" ]; then
            ${STRIP} -d ./skel/lib/modules/$KERNEL_VERSION/*.ko
    fi

    rm -f rootfs.cpio.gz
    ../scripts/build_rootfs.sh c rootfs.cpio.gz > /dev/null
    rm -rf skel
    cd - > /dev/null
}

longan_modify_init()
{
    sed -i '/\(\[ -x \/mnt\/sbin\/init\)/c\if [ -x /mnt/init ]; then\n    mount -n --move /proc /mnt/proc\n    mount -n --move /sys /mnt/sys\n    mount -n --move /dev /mnt/dev\n    exec switch_root /mnt /init\nfi\n' ./skel/init
}

update_roofs_for_longan()
{
    local rootfs_cpio="./rootfs.cpio.gz"

    echo "update roofs for longan ...."

    cd ${LICHEE_KDIR}/output
    if [ -x "../scripts/build_rootfs.sh" ]; then
        ../scripts/build_rootfs.sh e ${rootfs_cpio} > /dev/null
    else
        echo "No such file: scripts/build_rootfs.sh"
        exit 1
    fi

    longan_modify_init

    rm -f ${rootfs_cpio}
    ../scripts/build_rootfs.sh c ${rootfs_cpio} > /dev/null
    rm -rf skel
    cd - > /dev/null
}

build_ramfs()
{
    local kernel_size=0
    local CHIP="";

    if [ "${LICHEE_COMPRESS}" = "gzip" ]; then
	if [ "${ARCH}" = "arm" ]; then
		local BIMAGE="output/zImage";
	elif [ "${ARCH}" = "arm64" -o "${ARCH}" = "riscv" ]; then
		local BIMAGE="output/Image.gz";
	fi
    else
	local BIMAGE="output/bImage";
    fi

    local RAMDISK="output/rootfs.cpio.gz";
    local TINYOS_RAMDISK="${LICHEE_BR_OUT}/images/rootfs.cpio.gz"
    local BASE="";
    local RAMDISK_OFFSET="";
    local KERNEL_OFFSET="";
    local DTB_OFFSET="0x0"

    if [ "x${LICHEE_LINUX_DEV}" = "xtinyos" -a -f ${TINYOS_RAMDISK} ];then
        RAMDISK=${TINYOS_RAMDISK}
    else
        # update rootfs.cpio.gz with new module files
        regen_rootfs_cpio

        if [ "x${LICHEE_LINUX_DEV}" = "xlongan" ]; then
            update_roofs_for_longan
        fi
    fi

    CHIP=`echo ${LICHEE_CHIP} | sed -e 's/.*\(sun[0-9x]*i\).*/\1/g'`;

    if [ "${CHIP}" = "sun9i" ]; then
        BASE="0x20000000";
    else
        BASE="0x40000000";
    fi

    if [ "${ARCH}" = "arm" ]; then
        KERNEL_OFFSET="0x8000";
    elif [ "${ARCH}" = "arm64" ]; then
        KERNEL_OFFSET="0x80000";
    fi

    if [ -f output/bImage ]; then
       kernel_size="`stat output/bImage --format="%s"`"
    fi
    DTB_OFFSET=`printf "(%d+%d+%d)/%d*%d\n" $KERNEL_OFFSET $kernel_size 0x1fffff 0x100000 0x100000 | bc`
    RAMDISK_OFFSET=`printf "%d+%d\n" $DTB_OFFSET 0x100000 | bc`

    ${MKBOOTIMG} --kernel ${BIMAGE} \
        --ramdisk ${RAMDISK} \
        --board ${CHIP}_${ARCH} \
        --base ${BASE} \
        --kernel_offset ${KERNEL_OFFSET} \
        --ramdisk_offset ${RAMDISK_OFFSET} \
        --dtb_offset ${DTB_OFFSET} \
        -o output/boot.img

    # If uboot use *bootm* to boot kernel, we should use uImage.
    echo build_ramfs
    echo "Copy boot.img to output directory ..."
    cp output/boot.img ${LICHEE_PLAT_OUT}
    cp output/vmlinux.tar.bz2 ${LICHEE_PLAT_OUT}

    if [ -f output/zImage ] || [ -f output/uImage ]; then
        cp output/[zu]Image ${LICHEE_PLAT_OUT}
    fi

    if [ ! -f output/arisc ]; then
        echo "arisc" > output/arisc
    fi
    cp output/arisc ${LICHEE_PLAT_OUT}

    if [ "x${ARCH}" = "xarm64" ]; then
        if [ -f arch/${ARCH}/boot/dts/sunxi/board.dtb ]; then
            cp arch/${ARCH}/boot/dts/sunxi/board.dtb output/sunxi.dtb
        elif [ -f arch/${ARCH}/boot/dts/sunxi/${LICHEE_CHIP}-${LICHEE_BOARD}.dtb ]; then
            cp arch/${ARCH}/boot/dts/sunxi/${LICHEE_CHIP}-${LICHEE_BOARD}.dtb output/sunxi.dtb
        elif [ -f arch/${ARCH}/boot/dts/sunxi/${LICHEE_CHIP}-soc.dtb ]; then
            cp arch/${ARCH}/boot/dts/sunxi/${LICHEE_CHIP}-soc.dtb output/sunxi.dtb
        else
            echo "sunxi.dtb" > output/sunxi.dtb
       fi
    else
        if [ -f arch/${ARCH}/boot/dts/board.dtb ]; then
            cp arch/${ARCH}/boot/dts/board.dtb output/sunxi.dtb
        elif [ -f arch/${ARCH}/boot/dts/${LICHEE_CHIP}-${LICHEE_BOARD}.dtb ]; then
            cp arch/${ARCH}/boot/dts/${LICHEE_CHIP}-${LICHEE_BOARD}.dtb output/sunxi.dtb
        elif [ -f arch/${ARCH}/boot/dts/${LICHEE_CHIP}-soc.dtb ]; then
            cp arch/${ARCH}/boot/dts/${LICHEE_CHIP}-soc.dtb output/sunxi.dtb
        else
            echo "sunxi.dtb" > output/sunxi.dtb
        fi
    fi

    # It's used for dtb debug
    ./scripts/dtc/dtc -I dtb -O dts -o output/.sunxi.dts output/sunxi.dtb -W no-unit_address_vs_reg

    cp output/.sunxi.dts ${LICHEE_PLAT_OUT}
    cp output/sunxi.dtb ${LICHEE_PLAT_OUT}
}

gen_output()
{
    if [ "x${LICHEE_PLATFORM}" = "xandroid" ] ; then
        echo "Copy modules to target ..."
        rm -rf ${LICHEE_PLAT_OUT}/lib
        cp -rf ${LICHEE_KDIR}/output/* ${LICHEE_PLAT_OUT}
        return
    fi

    if [ -d ${LICHEE_BR_OUT}/target ] ; then
        echo "Copy modules to target ..."
        local module_dir="${LICHEE_BR_OUT}/target/lib/modules"
        rm -rf ${module_dir}
        mkdir -p ${module_dir}
        cp -rf ${LICHEE_MOD_DIR} ${module_dir}
    fi

    echo "$0"
}

clean_kernel()
{
    clarg="clean"
    if [ "x$1" == "xdistclean" ]; then
        clarg="distclean"
    fi
    echo "Cleaning kernel, arg: $clarg ..."
    make ARCH=${ARCH} "$clarg"
    rm -rf output/*
    echo
}

clean_modules()
{
    echo "Cleaning modules ..."
    clean_gpu
    echo
}

deal_verity_utils()
{
	cd ${LICHEE_KDIR}/output

    if [ "${ARCH}" = "arm" ]; then
        local verity_tools_dir="../verity_tools/32_bit";
    elif [ "${ARCH}" = "arm64" ]; then
        local verity_tools_dir="../verity_tools/64_bit";
    fi
	if [ ! -d ${verity_tools_dir} ]; then
		if [ x"$2" = x"clean" ];then
			#no tools, obviously dont need to clean, do nothing
			exit 0
		fi
	fi

    if [ -x "../scripts/build_rootfs.sh" ]; then
        ../scripts/build_rootfs.sh e ./rootfs.cpio.gz > /dev/null
    else
        echo "No such file: scripts/build_rootfs.sh"
        exit 1
    fi

	if [ x"$2" = x"clean" ];then
		rm -f skel/verity_key
		rm -f skel/verityInfo
	else
		local blk_size=$2
		local verity_dev=$3
		local ROOTFSTYPE=$4
		local key_file=$5

		#echo "blk_size="${blk_size}
		#echo "verity_dev="${verity_dev}
		#echo "ROOTFSTYPE="${ROOTFSTYPE}

		echo "blk_size="${blk_size} > skel/verityInfo
		echo "verity_dev="${verity_dev} >> skel/verityInfo
		echo "ROOTFSTYPE="${ROOTFSTYPE} >> skel/verityInfo

		if [ ! -f skel/usr/bin/openssl ]; then
			if [ -f ${verity_tools_dir}/openssl ]; then
				echo "ramfs do not have openssl bin, use a prebuild one"
				cp ${verity_tools_dir}/openssl skel/usr/bin
			else
				echo "no avaliable openssl found, can not enable dm-verity"
				exit 2
			fi
		fi

		if [ ! -f skel/usr/sbin/veritysetup ]; then
			if [ -f ${verity_tools_dir}/veritysetup ]; then
				echo "ramfs do not have veritysetup bin, use a prebuild one"
				cp ${verity_tools_dir}/veritysetup skel/usr/sbin
			else
				echo "no avaliable veritysetup found, can not enable dm-verity"
				exit 2
			fi
		fi
		cp ${key_file} skel/verity_key
	fi

    rm -f rootfs.cpio.gz
    ../scripts/build_rootfs.sh c rootfs.cpio.gz > /dev/null
	rm -rf skel1
	mv -f skel skel1
    cd - > /dev/null

    if [ "${LICHEE_COMPRESS}" = "gzip" ]; then
	if [ "${ARCH}" = "arm" ]; then
		local BIMAGE="output/zImage";
	elif [ "${ARCH}" = "arm64" -o "${ARCH}" = "riscv" ]; then
		local BIMAGE="output/Image.gz";
	fi
    else
	local BIMAGE="output/bImage";
    fi

    local RAMDISK="output/rootfs.cpio.gz";
    CHIP=`echo ${LICHEE_CHIP} | sed -e 's/.*\(sun[0-9x]*i\).*/\1/g'`;

    if [ "${CHIP}" = "sun9i" ]; then
        BASE="0x20000000";
    else
        BASE="0x40000000";
    fi

    if [ "${ARCH}" = "arm" ]; then
        KERNEL_OFFSET="0x8000";
    elif [ "${ARCH}" = "arm64" ]; then
        KERNEL_OFFSET="0x80000";
    fi
    if [ -f vmlinux ]; then
        bss_offset=`${CROSS_COMPILE}readelf -S vmlinux | \
            awk  '/\.bss/ {print $5}'`;
        bss_size=`${CROSS_COMPILE}readelf -S vmlinux | \
            awk  '/\.bss/ {print $6}'`;

        # If linux-4.9 the bss_size will be in next line
        if [ -z "$bss_size" ]; then
            bss_size=`${CROSS_COMPILE}readelf -S vmlinux | \
            awk  '/\.bss/ {getline var; print var}' | awk '{print $1}'`;
        fi

        bss_sz=$[ $((16#$bss_offset))+$((16#$bss_size))]
    fi

    # If the size of bImage larger than 16M, will offset 0x02000000
    if [ ${bss_sz} -gt $((16#1000000)) ]; then
        RAMDISK_OFFSET="0x02000000";
    else
        RAMDISK_OFFSET="0x01000000";
    fi

    ${MKBOOTIMG} --kernel ${BIMAGE} \
        --ramdisk ${RAMDISK} \
        --board ${CHIP}_${ARCH} \
        --base ${BASE} \
        --kernel_offset ${KERNEL_OFFSET} \
        --ramdisk_offset ${RAMDISK_OFFSET} \
        -o output/boot.img
    cp output/boot.img ${LICHEE_PLAT_OUT}
}

#####################################################################
#
#                      Main Runtine
#
#####################################################################

case "$1" in
    kernel)
        build_kernel
        ;;
    modules)
        build_modules
        ;;
    clean|distclean)
        clean_modules
        clean_kernel "$1"
        ;;
    deal_verity)
		deal_verity_utils $@
		;;
    dts)
        build_dts
        ;;
    *)
        build_kernel
        build_modules
        build_ramfs
        gen_output
        echo -e "\n\033[0;31;1m${LICHEE_CHIP} compile Kernel successful\033[0m\n\n"
        ;;
esac
