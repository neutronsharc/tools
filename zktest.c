#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


#include <sys/time.h>
#include <unistd.h>
#include <sys/select.h>

#include <time.h>
#include <errno.h>
#include <assert.h>

#include "zkutil.h"


int main(int argc, char** argv) {

  if (argc < 2) {
    printf("Usage: %s <zk-host-list>\n", argv[0]);
    return -1;
  }
  int log_level = ZOO_LOG_LEVEL_INFO;
  //ZOO_LOG_LEVEL_DEBUG;
  zoo_set_debug_level(log_level);

  char* zk_hosts = argv[1];

  zhandle_t* zkh = NULL;
  assert((zkh = ZKConnect(zk_hosts)) != NULL);

  int rc;
  int flags = 0;
  char path[2000];
  char value[100];
  char *base_znode = "/zktest1";
  sprintf(path, "%s/node-lev-1", base_znode);
  sprintf(value, "this is a test value");

  // 1, delete the node if exists
  if (ZKExists(zkh, path)) {
    printf("delete path %s\n", path);
    ZKDelete(zkh, path);
  }

  // 2, create
  ZKCreate(zkh, path, value, strlen(value), flags | ZOO_EPHEMERAL);
  sprintf(value, "this is a test value--v2");
  ZKSet(zkh, path, value, strlen(value));

  // 3, read node
  int watch = 1;
  int sync = 0;
  int value_len = 0;
  struct Stat stat;
  memset(value, 0, sizeof(value));
  rc = ZKGet(zkh, path, value, 100, watch, sync);
  fprintf(stderr, "ZKGet() znode \"%s\", ret %d, value len %d, value %s\n",
          path, rc, value_len, value);

  // 4, watch
  printf("Set a get-watcher at znode %s\n", path);
  ZKSetGetWatch(zkh, path, DefaultGetWatcher, NULL);

  // Another node

  sprintf(path, "%s/node-lev-2", base_znode);
  sprintf(value, "this is a test value for lev 2");
  if (ZKExists(zkh, path)) {
    printf("delete path %s\n", path);
    ZKDelete(zkh, path);
  }
  ZKCreate(zkh, path, value, strlen(value), flags);

  printf("Set a get-watcher at znode %s\n", path);
  ZKSetGetWatch(zkh, path, DefaultGetWatcher, NULL);

  sprintf(path, "%s/node-lev-3", base_znode);
  if (ZKExists(zkh, path)) {
    printf("delete path %s\n", path);
    ZKDelete(zkh, path);
  }
  printf("Set a exists-watcher at znode %s\n", path);
  ZKSetExistsWatch(zkh, path, DefaultExistsWatcher, NULL);

  sleep(600);


  // Close.
  ZKClose(zkh);

  sleep(10);
}
