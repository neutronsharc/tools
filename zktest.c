#include <zookeeper.h>
#include <zookeeper_log.h>
#include <proto.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


#include <sys/time.h>
#include <unistd.h>
#include <sys/select.h>

#include <time.h>
#include <errno.h>
#include <assert.h>


static zhandle_t *zk_handle;
static clientid_t myid;
static const char *clientIdFile = 0;
struct timeval startTime;
static char cmd[1024];
static int batchMode=0;

static int to_send=0;
static int sent=0;
static int recvd=0;

static int shutdownThisThing=0;

static const char* State2String(int state){
  if (state == 0)
    return "CLOSED_STATE";
  if (state == ZOO_CONNECTING_STATE)
    return "CONNECTING_STATE";
  if (state == ZOO_ASSOCIATING_STATE)
    return "ASSOCIATING_STATE";
  if (state == ZOO_CONNECTED_STATE)
    return "CONNECTED_STATE";
  if (state == ZOO_EXPIRED_SESSION_STATE)
    return "EXPIRED_SESSION_STATE";
  if (state == ZOO_AUTH_FAILED_STATE)
    return "AUTH_FAILED_STATE";

  return "INVALID_STATE";
}

static const char* Type2String(int state){
  if (state == ZOO_CREATED_EVENT)
    return "CREATED_EVENT";
  if (state == ZOO_DELETED_EVENT)
    return "DELETED_EVENT";
  if (state == ZOO_CHANGED_EVENT)
    return "CHANGED_EVENT";
  if (state == ZOO_CHILD_EVENT)
    return "CHILD_EVENT";
  if (state == ZOO_SESSION_EVENT)
    return "SESSION_EVENT";
  if (state == ZOO_NOTWATCHING_EVENT)
    return "NOTWATCHING_EVENT";

  return "UNKNOWN_EVENT_TYPE";
}


void watcher(zhandle_t *zh,
             int type,
             int state,
             const char *path,
             void* context) {
  // Be careful using zh here rather than zh - as this may be mt code
  // the client lib may call the watcher before zookeeper_init returns


  fprintf(stderr, "Watcher %s state = %s", Type2String(type), State2String(state));
  if (path && strlen(path) > 0) {
    fprintf(stderr, " for path %s", path);
  }
  fprintf(stderr, "\n");

  if (type == ZOO_SESSION_EVENT) {
    if (state == ZOO_CONNECTED_STATE) {
      const clientid_t *id = zoo_client_id(zh);
      if (myid.client_id == 0 || myid.client_id != id->client_id) {
        myid = *id;
        fprintf(stderr, "Got a new session id: 0x%llx\n",
                (long long)myid.client_id);
      } else {
        fprintf(stderr, "Same session repeated: 0x%llx\n",
                (long long)myid.client_id);
      }
    } else if (state == ZOO_AUTH_FAILED_STATE) {
      fprintf(stderr, "Authentication failure. Shutting down...\n");
      zookeeper_close(zh);
    } else if (state == ZOO_EXPIRED_SESSION_STATE) {
      fprintf(stderr, "Session expired. Shutting down...\n");
      zookeeper_close(zh);
    } else {
      fprintf(stderr, "SESSION event: ignore state: %s\n",
              State2String(state));
    }
  } else {
    fprintf(stderr, "Ignore type %s, state %s\n",
            Type2String(type), State2String(state));
  }
}

void my_string_completion(int rc, const char *name, const void *data) {
  fprintf(stderr, "rc = %d, name = %s\n", rc, name);
}

int main(int argc, char** argv) {

  if (argc < 2) {
    printf("Usage: %s <zk-host-list>\n", argv[0]);
    return -1;
  }
  int log_level = ZOO_LOG_LEVEL_DEBUG;
  zoo_set_debug_level(log_level);

  char* zk_hosts = argv[1];

  zk_handle = zookeeper_init(zk_hosts, watcher, 30000, &myid, 0, 0);

  int rc;
  int flags = 0;
  char path[2000];
  char value[100];
  char *base_znode = "/mynode1";
  sprintf(path, "%s/zktest1", base_znode);
  sprintf(value, "this is a test value");

  // 1, delete the node if exists
  rc = zoo_delete(zk_handle, path, -1);
  fprintf(stderr, "zoo_delete znode \"%s\", ret %d\n",
          path, rc);

  // 2, create node
  rc = zoo_create(zk_handle,
                  path,
                  value,
                  strlen(value),
                  &ZOO_OPEN_ACL_UNSAFE,
                  flags,
                  NULL,
                  0);
  fprintf(stderr, "zoo_create() znode \"%s\", ret %d\n",
          path, rc);

  // 3, read node
  int watch = 1;
  int value_len = 0;
  struct Stat stat;
  memset(value, 0, sizeof(value));
  rc = zoo_get(zk_handle,
               path,
               watch,
               value,
               &value_len,
               &stat);
  fprintf(stderr, "zoo_get() znode \"%s\", ret %d, value = %s\n",
          path, rc, value);


  sleep(20);
  // 4, delete node


  zookeeper_close(zk_handle);
}
