#!/bin/bash

if [ $# -lt 3 ]; then
  echo "Usage: $0 <my zk id> <zk dir> <zk data dir> [optional: existing zkaddr]"
  exit
fi


MYID=$1
ZKDIR=$2
ZKDATADIR=$3
EXISTINGZK=$4

HOSTNAME=`hostname`
IPADDRESS=`ip -4 addr show scope global dev eth0 | grep inet | awk '{print \$2}' | cut -d / -f 1`

CFG=conf/zoo.cfg
CFGDYN=conf/zoo.cfg.dynamic

cd $ZKDIR

# generate the static config file.
echo "standaloneEnabled=false" > $CFG
echo "dataDir=$ZKDATADIR" >> $CFG
echo "syncLimit=2" >> $CFG
echo "tickTime=2000" >> $CFG
echo "initLimit=5" >> $CFG
echo "maxClientCnxns=0" >> $CFG
echo "dynamicConfigFile=$ZKDIR/$CFGDYN" >> $CFG

./bin/zkServer.sh stop

## prepare zk data dir.
if [ ! -d $ZKDATADIR ]; then
  mkdir $ZKDATADIR
  echo "will initialize zk data dir $ZKDATADIR"
  ./bin/zkServer-initialize.sh --force --myid=$MYID
else
  # TODO: check if the data dir is valid. If not valid, should force initialize.
  # TODO: check dir permission??
fi

if [ -n "$EXISTINGZK" ]; then
  ## join an existing quorum
  str=`./bin/zkCli.sh -server $EXISTINGZK get /zookeeper/config | grep ^server`
  echo "existing servers: $str"
  echo $str > $CFGDYN
  echo "server.$MYID=$IPADDRESS:2888:3888;2181" >> $CFGDYN
  cp $CFGDYN $CFGDYN-origin
  ./bin/zkServer.sh start
  ./bin/zkCli.sh -server $EXISTINGZK reconfig -add "server.$MYID=$IPADDRESS:2888:3888;2181"
  ./bin/zkServer.sh stop
  sleep 1
  ./bin/zkServer.sh start
  echo "has joined an existing zk quorum $EXISTINGZK"
else
  # start alone in a new quorum
  ./bin/zkServer.sh stop
  echo "server.$MYID=$IPADDRESS:2888:3888;2181" > $CFGDYN
  ./bin/zkServer.sh start
  echo "has started myself as a new zk quorum $IPADDRESS:2181"
fi

