#!/bin/bash

proxy_addr="hcd1-10g"
proxy_port=21000
stats_addr="hcd1"
stats_port=31000
zk="ceph1:2181,ceph2:2181,ceph4:2181"

exe="./redis_bench"

GB=$((1024*1024*1024))
MB=$((1024*1024))

objsize=4000
time=600

# number of clients to launch. Each client spawn $threads worker threads.
instances=16
threads=4

# each client target read bw.
read_bw=$((2 * $GB))
# each thread target read qps
read=$(($read_bw / $objsize / $threads))

# each client target write bw
#write_bw=$((2 * $MB))
write_bw=$((0 * $MB))
# each thread target write qps
write=$(($write_bw / $objsize / $threads))

# Each client populate this much data during init.
client_data_size=$((1 * $GB))
nobjs=$(($client_data_size / $objsize))

mget=4

for (( i = 0; i < $instances; i++ )); do
  p1=$(($proxy_port + $i))

  $exe -a $proxy_addr -p $p1 -s $objsize -n $nobjs -t $threads -r $read \
    -w $write -i $time -m $mget -o &

done
