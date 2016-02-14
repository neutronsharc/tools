#!/usr/bin/python

import sys
import pycurl
import json
import time
from StringIO import StringIO
from urllib import urlencode

server_url = "http://xps3:3300"

def PutToUrl(url, data):
    """
    run "curl -X POST" to post data to url.

    "param url :  server url
    :param data:  must be a dict
    """
    buffer = StringIO()

    c = pycurl.Curl()
    c.setopt(c.URL, url)
    c.setopt(c.WRITEDATA, buffer)
    #c.setopt(c.WRITEFUNCTION, buffer.write)
    c.setopt(c.VERBOSE, False)
    c.setopt(c.HTTPHEADER, ['Content-Type: application/json',
                            'Accept: application/json'])
    #c.setopt(c.PUT, 1)
    c.setopt(c.CUSTOMREQUEST, 'PUT')

    # encode the data to PUT
    #postfields = urlencode(data)
    c.setopt(c.POSTFIELDS, json.dumps(data))

    # run curl cmd
    c.perform()
    c.close()

    # show the response.
    #body = buffer.getvalue()
    #print body


def CreatePool(poolname, poolid, range_begin, range_end):
    url = "{}/api/pool".format(server_url)
    d = {}
    d["name"] = poolname
    d["id"] = poolid
    d["pool_begin"] = range_begin
    d["pool_end"] = range_end
    d["mode"] = "create"
    d["children"] = []

    PutToUrl(url, d)


def CreateShard(poolname, poolid, shardname, shardid,
                range_begin, range_end):
    url = "{}/api/shard".format(server_url)
    d = {
        "id" : shardid,
        "name": shardname,
        "pool_name": poolname,
        "pool_id": poolid,
        "shard_begin": range_begin,
        "shard_end": range_end,
        "master_target": "",
        "slave_target": [],
        "mode": "create"}
    PutToUrl(url, d)


def CreateTarget(poolid, shardid, name, id, is_master,
                 ip, shard_port, rpc_port, http_port,
                 data_dir, wal_dir):
    url = "{}/api/target".format(server_url)
    d = {
        "mode": "create",
        "pool_id": poolid,
        "shard_id": shardid,
        "name": name,
        "id": id,
        "targetType": "master" if is_master else "slave",
        "ip": ip,
        "shardPort": shard_port,
        "rpcPort": rpc_port,
        "httpPort": http_port,
        "dataDir": data_dir,
        "walDir": wal_dir,
        "from": "string",
        "split_shard_id": "string",
        "split_shard_name": "string",
        "split_point": "string",
        "userid": "hcd",
        "password": "hcd"}
    PutToUrl(url, d)


def main():
    pname = "testpool1"
    poolid = "pool1id"
    range_begin = 0
    range_end = 1000000000

    num_shards = 8
    sname = "shardname"
    sid = "shardid"

    tname = "tgtname"
    tid = "tgtid"
    port = 10000
    port_interval = 6
    host = "hcd2-10g"

    # create pool
    print "will create pool {}".format(pname)
    CreatePool(pname, poolid, range_begin, range_end)
    time.sleep(1)

    # create shard
    interval = (range_end - range_begin) / num_shards
    nxt_range_begin = range_begin
    for i in range(num_shards):
        rb = nxt_range_begin
        nxt_range_begin = rb + interval
        re = nxt_range_begin - 1

        if (i == num_shards - 1):
            re = range_end

        name = sname + str(i)
        id = sid + str(i)

        print 'will create shard {} ({}), range [{} : {}]'.format(
               name, id, rb, re)

        CreateShard(pname, poolid, name, id, rb, re)
        time.sleep(1)

    # create 1 master target at each shard.
    for i in range(num_shards):
        shardid = sid + str(i)
        name = tname + str(i)
        id = tid + str(i)
        data_dir = "/data/nvme{}/".format(i % 2)
        wal_dir = "data/nvme{}/wal".format(i % 2)

        print 'will create target {} ({}) at {}:{}'.format(
               name, id, host, data_dir)

        is_master = True
        CreateTarget(poolid, shardid, name, id, is_master,
                     host, port, port + 1, port + 2,
                     data_dir, wal_dir)
        time.sleep(1)

        port += port_interval

    sys.exit(0)

if __name__ == "__main__":
  main()

