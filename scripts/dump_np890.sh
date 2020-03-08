#!/bin/bash -e

mkpkgdir=~/mips/OpenNoah/NoahSplit
mkpkg=$mkpkgdir/mkpkg
(cd $mkpkgdir; make -j8)

upd=$(ls -1 update_*.bin)
chmod 644 $upd

sudo umount -l dump/rootfs || true
sudo rm -rf dump export
mkdir -p dump export
cd dump
ln -sfr ../$upd update.bin
$mkpkg --type=np890 --extract update.bin dump.log

chmod 755 ploader sloader updtool
mv dump.log ploader sloader updtool ../export/

mkdir gz
mv *.gz gz
(cd gz; gunzip -N *.gz; gzip -9 *.8880)

mv _nand0.bin ../export/u-boot.bin || mv gz/loader.img ../export/ || mv gz/bloader ../export/
mv _nand1.bin ../export/zImage || mv gz/zImage ../export/

mkdir -p rootfs
([ -e _nand3.bin ] && sudo mount -o ro -t ext2 _nand3.bin rootfs) || \
([ -e gz/root.img ] && sudo mount -o ro -t ext2 gz/root.img rootfs)
([ -e _nand2.bin ] && sudo mount -o ro -t ext2 _nand2.bin rootfs/lib/modules) || \
([ -e gz/modules.img ] && sudo mount -o ro -t ext2 gz/modules.img rootfs/lib/modules)
([ -e _nand4.bin ] && sudo mount -o ro -t ext2 _nand4.bin rootfs/usr) || \
([ -e gz/usr.img ] && sudo mount -o ro -t ext2 gz/usr.img rootfs/usr)
([ -e _nand5.bin ] && sudo mount -o ro -t ext2 _nand5.bin rootfs/usr/local) || \
([ -e gz/local.img ] && sudo mount -o ro -t ext2 gz/local.img rootfs/usr/local)
([ -e _nand6.bin ] && sudo mount -o ro -t ext2 _nand6.bin rootfs/usr/local/share) || \
([ -e gz/share.img ] && sudo mount -o ro -t ext2 gz/share.img rootfs/usr/local/share)
[ -e _nand8.bin ] && sudo mount -o ro -t ext2 _nand8.bin rootfs/opt || true
[ -e _nand7.bin ] && sudo mount -o ro,noatime,codepage=936,iocharset=gb2312 -t vfat _nand7.bin rootfs/mnt/usbdisk || true
[ -e gz/usbdisk.img ] && sudo mount -o ro,noatime,codepage=936,iocharset=gb2312 -t vfat gz/usbdisk.img rootfs/mnt/usbdisk || true

mkdir tmp
sudo mount --bind tmp rootfs/tmp
[ -e gz/sysdata.img ] && mv gz/sysdata.img tmp/ || true
mv sysdata.img tmp/ || true
mv gz/*.8880.gz tmp/
mv gz/jj tmp/rescue.img || mv gz/rescue.img tmp/

sudo cp -a rootfs ../export/
sudo umount -l rootfs || true

arc=$(echo $upd | sed 's/^update_/np890_/;s/\.bin$/_dump.7z/')
cd -
#sudo tar acvf dump.tar.xz -C export .
(cd export; sudo 7za a ../$arc)
sudo rm -rf dump export
