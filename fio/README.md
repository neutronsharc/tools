# fio job file description


[global]
bs=4k
ioengine=libaio
iodepth=16
size=2g
direct=1
numjobs=8
#runtime=60
#runtime=40
;directory=/mount-point-of-ssd
#filename=/dev/sdg:/dev/sdh
filename=/dev/sdg

#[read-write]
#readwrite=randrw
#rwmixread=70
#stonewall

[seq-write]
rw=write
stonewall
group_reporting

[seq-read]
rw=read
stonewall
group_reporting

