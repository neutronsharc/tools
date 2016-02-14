#!/bin/bash

proxy_addr="hcd1-10g"
proxy_port=21000
stats_addr="hcd1"
stats_port=31000
zk="ceph1:2181,ceph2:2181,ceph4:2181"

exe="./redis_bench"

instances=8
objsize=4000
time=2000
threads=10
read=1000000
write=0

GB=$((1024*1024*1024))
total_size=$((8 * $GB))
nobjs=$(($total_size / $objsize))

for (( i = 0; i < $instances; i++ )); do
  p1=$(($proxy_port + $i))

  $exe -a $proxy_addr -p $p1 -s $objsize -n $nobjs -t $threads -r $read -w $write -i $time -o &

done
