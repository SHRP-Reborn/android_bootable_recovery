#!/bin/sh
recBlock=$recoveryBlock

#Creating env
mkdir -p tmp/work
cd tmp/work

#Pulling Recovery From Block
dd if=$recBlock of=recovery.img

#unpacking rec
/system/bin/magiskboot unpack -h recovery.img
