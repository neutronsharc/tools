#!/bin/bash

if [ $# -lt 2 ]; then
  echo "Usage: $0 <my zk id> <zk data dir> [optional: existing zk ip:2181]"


MYID=$1
ZKDATADIR=$2
EXISTINGZK=$3

ZKDIR=XXX

HOSTNAME=`hostname`
IPADDRESS=`ip -4 addr show scope global dev eth0 | grep inet | awk '{print \$2}' | cut -d / -f 1`
cd $ZKDIR

CFG=conf/zoo.cfg
CFGDYN=conf/zoo.cfg.dynamic

# generate the static config file.
echo "standaloneEnabled=false" > $CFG
echo "dataDir=$ZKDATADIR" >> $CFG
echo "syncLimit=2" >> $CFG
echo "tickTime=2000" >> $CFG
echo "initLimit=5" >> $CFG
echo "maxClientCnxns=0" >> $CFG
echo "dynamicConfigFile=$ZKDIR/$CFGDYN" >> $CFG

if [ ! -d $ZKDATADIR ]; then
  mkdir $ZKDATADIR
fi

# TODO: check dir permission??

if [ -n "$EXISTINGZK" ]; then
  ## join an existing quorum
  ./bin/zkServer.sh stop
  str=`./bin/zkCli.sh -server $EXISTINGZK get /zookeeper/config | grep ^server`
  echo "existing servers: $str"
  echo $str > $CFGDYN
  echo "server.$MYID=$IPADDRESS:2888:3888;2181" >> $CFGDYN
  cp $CFGDYN $CFGDYN-origin
  ./bin/zkServer-initialize.sh --force --myid=$MYID
  ./bin/zkServer.sh start
  ./bin/zkCli.sh -server $EXISTINGZK reconfig -add "server.$MYID=$IPADDRESS:2888:3888;2181"
  ./bin/zkServer.sh stop
  sleep 1
  ./bin/zkServer.sh start
  echo "has joined an existing zk quorum $ZK"
else
  # start alone in a new quorum
  ./bin/zkServer.sh stop
  echo "server.$MYID=$IPADDRESS:2888:3888;2181" > $CFGDYN
  ./bin/zkServer-initialize.sh --force --myid=$MYID
  ./bin/zkServer.sh start
  echo "has started myself as a new zk quorum $IPADDRESS:2181"
fi
