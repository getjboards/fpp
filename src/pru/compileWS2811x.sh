#!/bin/sh

echo $0 $@
cd /tmp
/bin/rm -f /opt/fpp/lib/FalconWS281x.*

# clpru versions
/usr/bin/cpp -P $@ /opt/fpp/src/pru/FalconWS281x.asm > /tmp/FalconWS281x.asm
clpru -v3 -o  $@ /tmp/FalconWS281x.asm
clpru -v3 -z --entry_point main /opt/fpp/src/pru/AM335x_PRU.cmd -o FalconWS281x.out FalconWS281x.obj
#objdump -h FalconWS281x.out | grep .text |  awk '{print "dd if='FalconWS281x.out' of='FalconWS281x.bin' bs=1 count=$[0x" $3 "] skip=$[0x" $6 "]"}'  | bash

