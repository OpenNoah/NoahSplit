#!/bin/bash -e

mkpkgdir=~/mips/OpenNoah/NoahSplit
mkpkg=$mkpkgdir/mkpkg
(cd $mkpkgdir; make -j8)

upd=update_*.bin
chmod 644 $upd

sudo umount -l dump/rootfs || true
sudo rm -rf dump export
mkdir -p dump export
cd dump
ln -sfr ../$upd update.bin
$mkpkg --type=np890 --extract update.bin dump.log

chmod 755 ploader sloader updtool
mv dump.log ploader sloader updtool ../export/

mkdir sys
mv sys*.gz sys
(cd sys; gunzip -N *.gz; gzip -9 *.8880)

mkdir -p rootfs
[ -e _nand3.bin ] && sudo mount -o ro -t ext2 _nand3.bin rootfs || true
[ -e _nand2.bin ] && sudo mount -o ro -t ext2 _nand2.bin rootfs/lib/modules || true
[ -e _nand4.bin ] && sudo mount -o ro -t ext2 _nand4.bin rootfs/usr || true
[ -e _nand5.bin ] && sudo mount -o ro -t ext2 _nand5.bin rootfs/usr/local || true
[ -e _nand6.bin ] && sudo mount -o ro -t ext2 _nand6.bin rootfs/usr/local/share || true
[ -e _nand8.bin ] && sudo mount -o ro -t ext2 _nand8.bin rootfs/opt || true
[ -e _nand7.bin ] && sudo mount -o ro,noatime,codepage=936,iocharset=gb2312 -t vfat _nand7.bin rootfs/mnt/usbdisk || true

mkdir tmp
sudo mount --bind tmp rootfs/tmp
mv sysdata.img tmp
mv sys/*.8880.gz tmp
mv sys/jj tmp/rescue.img

sudo cp -a rootfs ../export/
sudo umount -l rootfs || true

cd -
#sudo tar acvf dump.tar.xz -C export .
(cd export; sudo 7za a ../dump.7z)
sudo rm -rf dump export
