sudo apt-get install -y sed make binutils build-essential gcc g++ bash patch gzip bzip2 perl tar cpio unzip rsync file bc wget python cvs git mercurial rsync subversion android-tools-mkbootimg vim libssl-dev android-tools-fastboot


1. 连接wifi：
ifconfig wlan0 up
wifi -c OPPO5G 147zzz...

禁止wifi警告打印：echo 3 > /proc/sys/kernel/printk

2. 传输文件：
检测adb设备：adb devices
传输文件到adb设备：adb push test1.txt /root/
从adb设备拉取文件：adb pull xxx ./


3. 三套编译工具链：prebuilt/rootfsbuilt/arm目录下
    · gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-gcc：
        · none指普通开源版本的gcc，用来编译逻辑、uboot等。
    · toolchain-sunxi-glibc-gcc-830/toolchain/bin/arm-openwrt-linux-gnueabi-gcc：
        · 针对openwrt的工具链，使用了完善的glibc库。
    · toolchain-sunxi-musl-gcc-830/toolchain/bin/arm-openwrt-linux-muslgnueabi-gcc：
        ·针对openwrt的工具链，使用了精简的musl c库，板子使用这套工具链。因此，对于其它工具链必须使用静态编译才能运行，同时elf文件也会比较大。

## 使用musl工具链：
export PATH=$PATH:/home/rlk/weidongshan/v853sdk/tina-v853-open/prebuilt/rootfsbuilt/arm/toolchain-sunxi-musl-gcc-830/toolchain/bin/
export STAGING_DIR=/home/rlk/weidongshan/v853sdk/tina-v853-open/prebuilt/rootfsbuilt/arm/toolchain-sunxi-musl-gcc-830/toolchain/arm-openwrt-linux-muslgnueabi

arm-openwrt-linux-muslgnueabi-gcc

## 使用glibc工具链静态编译：
export PATH=$PATH:/home/rlk/weidongshan/v853sdk/tina-v853-open/prebuilt/rootfsbuilt/arm/toolchain-sunxi-glibc-gcc-830/toolchain/bin/
export STAGING_DIR=/home/rlk/weidongshan/v853sdk/tina-v853-open/prebuilt/rootfsbuilt/arm/toolchain-sunxi-glibc-gcc-830/toolchain/arm-openwrt-linux-gnueabi

arm-openwrt-linux-gnueabi-gcc -static 

3. 编译所有：sdk目录下
    source build/envsetup.sh
    lunch
    make -j20 && pack
    
# 打包可能分区溢出，修改分区表
    device/config/chips/v853/configs/100ask/linux-4.9/sys_partition.fex

## 只编译内核，然后打包成烧录镜像
    mkernel 
    cp /home/rlk/weidongshan/v853sdk/tina-v853-open/kernel/linux-4.9/output/uImage /home/rlk/weidongshan/v853sdk/tina-v853-open/out/v853/100ask/openwrt/build_dir/target/linux-v853-100ask/ -f
    cp /home/rlk/weidongshan/v853sdk/tina-v853-open/kernel/linux-4.9/output/zImage /home/rlk/weidongshan/v853sdk/tina-v853-open/out/v853/100ask/openwrt/build_dir/target/linux-v853-100ask/ -f
    cp /home/rlk/weidongshan/v853sdk/tina-v853-open/kernel/linux-4.9/output/bImage /home/rlk/weidongshan/v853sdk/tina-v853-open/out/v853/100ask/openwrt/build_dir/target/linux-v853-100ask/ -f
    pack

## 内核生成clangd索引文件，修改scripts/build.sh
	mv compile_commands.json compile_commands_bk.json
    LICHEE_JLEVEL=20
    bear make ARCH=${ARCH} CROSS_COMPILE="${CCACHE_Y}${CROSS_COMPILE}" -j${LICHEE_JLEVEL} ${arch_target} modules
    jq -s 'add' compile_commands_bk.json compile_commands.json > compile_commands_final.json	 # 实现增量编译
        rm compile_commands_bk.json compile_commands.json
        mv compile_commands_final.json compile_commands.json

4. 编译v4l2-utils


## 复制库和程序到根文件系统相关制作目录下
    cp -r bin /home/rlk/weidongshan/v853sdk/tina-v853-open/openwrt/target/v853/v853-100ask/busybox-init-base-files
    cp -r lib /home/rlk/weidongshan/v853sdk/tina-v853-open/openwrt/target/v853/v853-100ask/busybox-init-base-files


5. 参考地址：
https://tina.100ask.net/SdkModule/Linux_SystemSoftware_DevelopmentGuide-03/


6. 其它命令
命令	        命令有效目录	作用
make	                    tina根目录 编译整个sdk	
make menuconfig             tina根目录 启动软件包配置界面	
make kernel_menuconfig      tina根目录 启动内核配置界面	
mkarisc	        tina下任意目录 编译cpus源码，根据AXP型号选择对应的默认配置	
printfconfig	tina下任意目录 打印当前SDK的配置	
croot	        tina下任意目录 快速切换到tina根目录	
cconfigs	    tina下任意目录 快速切换到方案的bsp配置目录	
cdevice	        tina下任意目录 快速切换到方案配置目录	
carisc	        tina下任意目录 快速切换到cpus代码目录	
cgeneric	    tina下任意目录 快速切换到方案generic目录	
cout	        tina下任意目录 快速切换到方案的输出目录	
cboot	        tina下任意目录 快速切换到bootloader目录	
cgrep	        tina下任意目录 在c／c++／h文件中查找字符串	
minstall path/to/package/   tina根目录 编译并安装软件包	
mclean path/to/package/     tina根目录 clean软件包	
mm [-B]	        软件包目录 编译软件包，-B指编译前先clean	
pack	        tina根目录 打包固件	
m	            tina下任意目录 make的快捷命令，编译整个sdk	
p	            tina下任意目录 pack的快捷命令，打包固件