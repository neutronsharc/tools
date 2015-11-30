#include <zookeeper.h>
#include <zookeeper_log.h>
#include <proto.h>
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

static void CompletionForGet(int rc,
                      const char *value,
                      int value_len,
                      const struct Stat *stat,
                      const void *data);

static void WatcherForGet(zhandle_t *zkh,
                   int type,
                   int state,
                   const char *path,
                   void *ctx);

static void GlobalWatcher(zhandle_t *zh,
                   int type,
                   int state,
                   const char *path,
                   void* ctx);

int ZKGet(zhandle_t *zkh,
          char *path,
          char *buf,
          int buf_len,
          int watch,
          int sync);

int ZKCreate(zhandle_t *zkh, char* path, char *value, int value_len, int flags);

int ZKDelete(zhandle_t *zkh, char* path);

int ZKWatch(zhandle_t *zkh, char *path, watcher_fn watcher, void* ctx);

static int connected = 0;
static int expired = 0;

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

static const char* Type2String(int type){
  if (type == ZOO_CREATED_EVENT)
    return "CREATED_EVENT";
  if (type == ZOO_DELETED_EVENT)
    return "DELETED_EVENT";
  if (type == ZOO_CHANGED_EVENT)
    return "CHANGED_EVENT";
  if (type == ZOO_CHILD_EVENT)
    return "CHILD_EVENT";
  if (type == ZOO_SESSION_EVENT)
    return "SESSION_EVENT";
  if (type == ZOO_NOTWATCHING_EVENT)
    return "NOTWATCHING_EVENT";

  return "UNKNOWN_EVENT_TYPE";
}


// Context struct for zoo_get().
typedef struct {
  // zk path to get
  char *path;
  // buf to store returned value
  char *buf;
  // buf size in bytes
  int buf_len;

  // Return code.
  int rc;
  // returned value size in bytes
  int ret_buf_len;

  // sync lock
  pthread_mutex_t lock;
} ContextGet;

void GlobalWatcher(zhandle_t *zh,
                   int type,
                   int state,
                   const char *path,
                   void* ctx) {
  fprintf(stderr, "global_watcher: event type %s, state = %s",
          Type2String(type), State2String(state));
  if (path && strlen(path) > 0) {
    fprintf(stderr, " for path %s", path);
  }
  fprintf(stderr, "\n");
  if (type == ZOO_SESSION_EVENT) {
    if (state == ZOO_CONNECTED_STATE) {
      fprintf(stderr, "Zookeeper connected.\n");
      connected = 1;
    } else if (state == ZOO_AUTH_FAILED_STATE) {
      fprintf(stderr, "Authentication failure. Shutting down...\n");
      zookeeper_close(zh);
    } else if (state == ZOO_EXPIRED_SESSION_STATE) {
      connected = 0;
      expired = 1;
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

void CompletionForGet(int rc,
                      const char *value,
                      int value_len,
                      const struct Stat *stat,
                      const void *data) {
  if (data == NULL) {
    return;
  }
  ContextGet* ctx = (ContextGet*)data;
  ctx->rc = rc;

  switch (rc) {
    case ZOK:
      if (value_len > ctx->buf_len - 1) {
        ctx->ret_buf_len = ctx->buf_len - 1;
      } else {
        ctx->ret_buf_len = value_len;
      }
      memcpy(ctx->buf, value, ctx->ret_buf_len);
      ctx->buf[ctx->ret_buf_len] = 0;
      break;

    case ZCONNECTIONLOSS:
    case ZOPERATIONTIMEOUT:
      fprintf(stderr, "Get completion: path %s: error = %d\n",
              ctx->path, rc);
      break;

    default:
      break;
  }

  pthread_mutex_unlock(&ctx->lock);
}

// Watcher func for get().
void WatcherForGet(zhandle_t *zkh,
                   int type,
                   int state,
                   const char *path,
                   void *ctx) {
  fprintf(stderr, "watcher_get: got event %s, state %s at path %s\n",
          Type2String(type), State2String(state), path);

  if (type == ZOO_CHANGED_EVENT) {
    // only on zoo_exists and zoo_get
    char buf[4000];
    int watch = 0;
    int sync = 1;
    ZKGet(zkh, (char*)path, buf, sizeof(buf), watch, sync);

    // RE-arm watcher.
    ZKWatch(zkh, (char*)path, WatcherForGet, (void*)path);
  }

  if (type == ZOO_DELETED_EVENT) {
    // only on zoo_exists and zoo_get
    //zoo_awget(zkh, path, WatcherForGet, (void*)path, CompletionForGet, NULL);
  }


  // only on zoo_exists
  // ZOO_CREATED_EVENT:

      // only for zoo_get_children or zoo_get_children2
      // ZOO_CHILD_EVENT:

      // This is generated when a client loses contact
      // or reconnects with a server
      // ZOO_SESSION_EVENT:

      // when a server will no longer watch a node for a client.
      // ZOO_NOTWATCHING_EVENT:

}

int ZKCreate(zhandle_t *zkh, char* path, char *value, int value_len, int flags) {
  int rc = zoo_create(zkh,
                      path,
                      value,
                      value_len,
                      &ZOO_OPEN_ACL_UNSAFE,
                      flags,
                      NULL,
                      0);
  fprintf(stderr, "create znode \"%s\", ret %d\n",
          path, rc);
  return rc;
}

int ZKDelete(zhandle_t *zkh, char* path) {
  int rc = zoo_delete(zkh, path, -1);
  fprintf(stderr, "delete znode \"%s\", ret %d\n",
          path, rc);

  return rc;
}



// Read a znode, optionally put watcher on it.
//
// Recommend to pass sync = 0 so that we can use awget() to pass
// a customer-defined watcher func.
// If "sync == 1 and watch == 1",  will use global watcher.
int ZKGet(zhandle_t *zkh,
          char *path,
          char *buf,
          int buf_len,
          int watch,
          int sync) {

  struct Stat stat;
  ContextGet ctx;

  int rc;
  if (sync) {
    rc = zoo_get(zkh, path, watch, buf, &buf_len, &stat);
  } else {
    ctx.path = path;
    ctx.buf = buf;
    ctx.buf_len = buf_len;
    ctx.ret_buf_len = 0;

    pthread_mutex_init(&ctx.lock, NULL);
    pthread_mutex_lock(&ctx.lock);

    rc = zoo_awget(zkh,
                   path,
                   watch ? WatcherForGet : NULL,
                   watch ? path : NULL,
                   CompletionForGet,
                   &ctx);
  }
  if (rc != ZOK) {
    return rc;
  }

  if (sync) {
    fprintf(stderr, "ZKGet() sync: path %s: ret value \"%s\"(%d)\n",
            path, buf, buf_len);
  } else {
    pthread_mutex_lock(&ctx.lock);
    fprintf(stderr, "ZKGet() async: path %s: ret value \"%s\"(%d)\n",
            path, buf, ctx.ret_buf_len);
  }


  return ctx.rc;
}


int ZKWatch(zhandle_t *zkh, char *path, watcher_fn watcher, void* ctx) {
  int rc = zoo_awget(zkh,
                     path,
                     watcher,
                     ctx,
                     CompletionForGet,
                     NULL);
  return rc;
}

// Connect to zookeeper.
//
// Return: a zk handler.
zhandle_t* ZKConnect(char* zk_hosts) {
  if (connected) {
    fprintf(stderr, "zk %s already connected\n", zk_hosts);
    return NULL;
  }

  zhandle_t* zkh = NULL;
  zkh = zookeeper_init(zk_hosts,
                       GlobalWatcher,
                       30000,
                       NULL,
                       0,
                       0);
  // Wait for zk connection to established.
  while (!connected) {
    sleep(1);
  }

  return zkh;
}

// Close connection to zk.
void ZKClose(zhandle_t* zkh) {
  zookeeper_close(zkh);
  connected = 0;
  fprintf(stderr, "have closed zk...\n");
}


// Check if znode exists.
//
// Return:  1 if exists, 0 otherwise.
int Exists(zhandle_t *zkh, char* path) {
  struct Stat stat;
  int watch = 0;
  int rc = zoo_exists(zkh, path, watch, &stat);
  if (rc == ZOK) {
    return 1;
  } else {
    return 0;
  }
}

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
  printf("before delete: path %s exists = %d\n", path, Exists(zkh, path));
  ZKDelete(zkh, path);
  printf("after delete: path %s exists = %d\n", path, Exists(zkh, path));

  // 2, create
  ZKCreate(zkh, path, value, strlen(value), flags);
  printf("after create: path %s exists = %d\n", path, Exists(zkh, path));

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
  //ZKWatch(zkh, path, WatcherForGet, path);

  // Another node

  sprintf(path, "%s/node-lev-2", base_znode);
  sprintf(value, "this is a test value for lev 2");
  ZKDelete(zkh, path);
  ZKCreate(zkh, path, value, strlen(value), flags);

  ZKWatch(zkh, path, WatcherForGet, path);

  sleep(60);


  // Close.
  ZKClose(zkh);

  sleep(10);
}
