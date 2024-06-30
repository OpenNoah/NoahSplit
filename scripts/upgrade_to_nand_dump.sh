#!/bin/bash -x
src=upgrade.bin
pkg=upgrade.pkg
nand=nand_dump_oob.bin
qcow2=nand_dump_oob.qcow2

#../mkpkg --type=np1000 --extract $src $pkg
./create_nand_dump.py -s $((2*1024*1024*1024)) -p 4096 -o 128 $pkg $nand
qemu-img convert -f raw -O qcow2 $nand $qcow2
