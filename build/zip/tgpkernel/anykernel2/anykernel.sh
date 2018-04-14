# ------------------------------
# TGPKERNEL INSTALLER 5.6.2
#
# Anykernel2 created by @osm0sis
# Everything else done by @djb77
# ------------------------------

## AnyKernel setup
properties() {
kernel.string=
do.devicecheck=1
do.modules=0
do.cleanup=1
do.cleanuponabort=1
device.name1=dreamlte
device.name2=dream2lte
device.name3=
device.name4=
device.name5=
}

# Shell Variables
block=/dev/block/platform/11120000.ufs/by-name/BOOT
ramdisk=/tmp/anykernel/ramdisk
split_img=/tmp/anykernel/split_img
patch=/tmp/anykernel/patch
is_slot_device=0
ramdisk_compression=auto

## AnyKernel methods (DO NOT CHANGE)
# import patching functions/variables - see for reference
. /tmp/anykernel/tools/ak2-core.sh

## AnyKernel install
ui_print "- Extracing Boot Image"
dump_boot

# Ramdisk changes - Set split_img OSLevel depending on ROM
(grep -w ro.build.version.security_patch | cut -d= -f2) </system/build.prop > /tmp/rom_oslevel
ROM_OSLEVEL=`cat /tmp/rom_oslevel`
echo $ROM_OSLEVEL | rev | cut -c4- | rev > /tmp/rom_oslevel
ROM_OSLEVEL=`cat /tmp/rom_oslevel`
ui_print "- Setting security patch level to $ROM_OSLEVEL"
echo $ROM_OSLEVEL > $split_img/boot.img-oslevel

# Ramdisk changes - SELinux Enforcing Mode
if egrep -q "install=1" "/tmp/aroma/selinux.prop"; then
	ui_print "- Enabling SELinux Enforcing Mode"
	replace_string $ramdisk/init.rc "setenforce 1" "setenforce 0" "setenforce 1"
	replace_string $ramdisk/init.rc "SELINUX=enforcing" "SELINUX=permissive" "SELINUX=enforcing"
	replace_string $ramdisk/sbin/tgpkernel.sh "echo \"1\" > /sys/fs/selinux/enforce" "echo \"0\" > /sys/fs/selinux/enforce" "echo \"1\" > /sys/fs/selinux/enforce"
	replace_string $ramdisk/sbin/tgpkernel.sh "chmod 644 /sys/fs/selinux/enforce" "chmod 640 /sys/fs/selinux/enforce" "chmod 644 /sys/fs/selinux/enforce"
fi

# Ramdisk changes - Deodexed ROM
if egrep -q "install=1" "/tmp/aroma/deodexed.prop"; then
	ui_print "- Patching for Deodexed ROM"
	if egrep -q "install=1" "/tmp/aroma/check_s8.prop"; then
		cp -rf $patch/sepolicy-s8/* $ramdisk
	fi
	if egrep -q "install=1" "/tmp/aroma/check_n8.prop"; then
		cp -rf $patch/sepolicy-n8/* $ramdisk
	fi
	if egrep -q "install=1" "/tmp/aroma/check_s9.prop"; then
		sed -i -- 's/(allow zygote dalvikcache_data_file (file (ioctl read write create getattr setattr lock append unlink rename open)))/(allow zygote dalvikcache_data_file (file (ioctl read write create getattr setattr lock append unlink rename open execute)))/g' /system/etc/selinux/plat_sepolicy.cil
		echo "(allow zygote_26_0 dalvikcache_data_file_26_0 (file (execute)))" >> /system/vendor/etc/selinux/nonplat_sepolicy.cil
	fi
fi

# Ramdisk changes - Spectrum
if egrep -q "install=1" "/tmp/aroma/spectrum.prop"; then
	ui_print "- Adding Spectrum"
	cp -rf $patch/spectrum/* $ramdisk
	chmod 644 $ramdisk/init.spectrum.rc
	chmod 644 $ramdisk/init.spectrum.sh
	insert_line init.rc "import /init.spectrum.rc" after "import /init.services.rc" "import /init.spectrum.rc"
fi

## AnyKernel file attributes
# set permissions/ownership for included ramdisk files
chmod 644 $ramdisk/default.prop
chmod 755 $ramdisk/init.rc
chmod 755 $ramdisk/sbin/tgpkernel.sh
chown -R root:root $ramdisk/*

# End ramdisk changes
ui_print "- Writing Boot Image"
write_boot

## End install
ui_print "- Done"

