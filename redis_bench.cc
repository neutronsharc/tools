#include <assert.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
//#include <bsd/stdlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <openssl/sha.h>
#include <string>
#include <thread>
#include <typeinfo>
#include <hiredis.h>
#include <bsd/stdlib.h>

#include "debug.h"
#include "recorder.h"
#include "utils.h"
#include "hdr_histogram.h"

using namespace std;

#define CRASH_ON_FAILURE 0

#define MAX_OBJ_SIZE (5000000)

/////////////
// Integrity check error code
#define CHECK_OK             0
#define ERR_KEY_LEN_MISMATCH -2
#define ERR_KEY_MISMATCH     -3
#define ERR_BUF_LEN_MISMATCH -4
#define ERR_SHA1_MISMATCH    -5

// Benchmark configs.
int numTasks = 1;
vector<size_t> ioSizes;
double writeRatio = 0;
size_t runTimeSeconds = 10;
size_t initObjNumber = 1000;
size_t readTargetQPS = 1000;
size_t writeTargetQPS = 1000;
size_t numberShards = 1;

bool overwrite_all = false;
static bool check_data = false;
static bool randomize_data = true;

static int redis_server_port = -1;
static char *redis_server_ip = NULL;
static string size_str = "4000";

static int mget_batch_size = 1;

static uint32_t procid = -1;

// By default don't use exptime.
uint32_t expire_time = 0;

static int timer_cycle_sec = 2;

static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

static struct hdr_histogram *histogram_read = NULL;
static struct hdr_histogram *histogram_write = NULL;

// Operation stats.
struct OpStats {
  uint64_t reads;
  uint64_t read_size;
  uint64_t read_miss;
  uint64_t read_failure;

  uint64_t writes;
  uint64_t write_size;
  uint64_t write_failure;
};

// Context for each task thread.
struct TaskContext {
  // Task id.
  int id;

  // Redis context
  redisContext *redis_ctx;

  // whether this task runs write workload.
  bool doWrite;

  // Will run this many seconds.
  unsigned long runTimeSeconds;

  // ratio of write ops, 0.0 ~ 1.0. Main thread will set this value.
  //double writeRatio;

  // Number of write performed
  unsigned long writeOps;
  // How many bytes has this task written
  unsigned long writeBytes;

  // Target qps for write ops by this worker.
  unsigned long writeTargetQPS;

  unsigned long writeSuccess;
  unsigned long writeFailure;

  // Number of read performed.
  unsigned long readOps;

  // Total bytes read.
  unsigned long readBytes;

  // In mget mode, multi objs are read in on read op.
  unsigned long readObjs;

  // Target qps for read issued by this worker.
  unsigned long readTargetQPS;

  unsigned long readSuccess;
  unsigned long readFailure;

  unsigned long readHit;
  unsigned long readMiss;

  // How many times we re-connect to redis due to failures.
  uint64_t reconnects;

  // record operation latency in micro-sec.
  Recorder<unsigned int> *readRec;
  Recorder<unsigned int> *writeRec;

  // semaphores to sync with main thread.
  //sem_t sem_begin;
  //sem_t sem_end;

  // A lock to sync output.
  //mutex *outputLock;
};

struct TimerContext {
  timer_t *timer;

  // array of task contexts
  TaskContext* tasks;

  // number of tasks
  int ntasks;

  // cumulated stats of all tasks.
  OpStats  stats;

};


static void PrintStats(unsigned int *latency,
                       int size,
                       struct hdr_histogram *histogram,
                       const char *header);

static void PrintHistoStats(struct hdr_histogram *histogram,
                            const char *header);

unsigned long get_random(unsigned long max_val) {
  return std::rand() % max_val;
}

// Use atomic counter for: number of objs, next new obj id
std::atomic<size_t> objectNumber;

size_t NumberOfObjs() {
  return objectNumber.load();
}

size_t NextObjectID() {
  return objectNumber++;
}

unsigned long GetRandomID() {
  unsigned long currentObjs = NumberOfObjs();
  unsigned long shift = 1000000;
  unsigned long objID;

  if (currentObjs <= initObjNumber + shift) {
    objID = get_random(currentObjs);
  } else {
    objID = get_random(initObjNumber + shift) +
            (currentObjs - initObjNumber - shift);
  }

  // A race condition: write incremented the obj-cnt, but hasn't inserted
  // the obj into kvstore yet (due to eviction, etc).
  // At same time, reader may read an ID that's identical to the
  // outstanding writing.
  if (objID > 1000) {
    objID -= 1000;
  }

  return objID;
}

bool ShouldWrite(int max, int thresh) {
  unsigned long val = get_random(max);
  if (val < thresh) {
    return true;
  } else {
    return false;
  }
}

// Throttle to target QPS.
static void ThrottleForQPS(long targetQPS,
                           unsigned long startUsec,
                           unsigned long opsSinceStart) {
  unsigned long actualSpentTimeUs = NowInUsec() - startUsec;
  unsigned long targetSpentTimeUs =
    (unsigned long)(opsSinceStart * 1000000 / targetQPS);
  if (actualSpentTimeUs < targetSpentTimeUs) {
    usleep(targetSpentTimeUs - actualSpentTimeUs);
  }
}

// Encode given buffer with predefined format.
//
// After encoding, the buffer will be:
//
//   (header)
//     SHA1 of the rest of buffer (20 bytes)
//     int buf_len  :  total size of the buf (4 bytes)
//     int key_len  :  key length (4 bytes)
//     char key[]   :  key (keylen bytes)
//   (data)
//     char data[]  :  random data (buflen - 20 - 4 - 4 - keylen)
//
static void EncodeBuffer(char *buf, int buflen, char *key, int keylen) {
  int md_len = SHA_DIGEST_LENGTH;   // 20 bytes
  int hdr_size = md_len + sizeof(int) + sizeof(int) + keylen;
  assert(buflen > hdr_size);

  char *pt = buf + md_len;
  memcpy(pt, &buflen, sizeof(int));

  pt += sizeof(int);
  memcpy(pt, &keylen, sizeof(int));

  pt += sizeof(int);
  memcpy(pt, key, keylen);

  // Now compute sha-1 of the whole buf (excluding the leading md area)
  unsigned char hash[md_len];
  pt = buf + md_len;

  SHA1((const unsigned char*)pt, buflen - md_len, hash);
  memcpy(buf, hash, md_len);
}

// Check data integrity of the buf, which is encoded with
// EncodeBuffer().
//
// Return: integrity check error code.
//    0: success
//    < 0: error
static int CheckBuffer(char *buf, int buflen, char *key, int keylen) {
  int md_len = SHA_DIGEST_LENGTH;
  unsigned char hash[md_len];

  char *pt = buf + md_len;
  SHA1((const unsigned char*)pt, buflen - md_len, hash);

  if (memcmp(hash, buf, md_len) != 0) {
    return ERR_SHA1_MISMATCH;
  }

  if (*(int*)pt != buflen) {
    return ERR_BUF_LEN_MISMATCH;
  }

  pt += sizeof(int);
  if (*(int*)pt != keylen) {
    return ERR_KEY_LEN_MISMATCH;
  }

  pt += sizeof(int);
  if (memcmp(pt, key, keylen) != 0) {
    return ERR_KEY_MISMATCH;
  }

  return CHECK_OK;
}

// Read (k, v) from Redis.
//
// Return value:
//  -1  : failed to read.
//  -2  : data size mismatch
//  -3  : data corruption
//   0  : read miss
//  > 0 : data size
static int ReadObj(redisContext *redis_ctx,
                   char* key,
                   int nkey,
                   char* valbuf,
                   int expectDataSize) {
  int ret = 0;

  redisReply *reply = (redisReply *)redisCommand(redis_ctx, "GET %s", key);
  if (!reply) {
    ret = -1;
  } else if (reply->type != REDIS_REPLY_STRING || reply->str == NULL) {
    ret = 0;
    printf("read miss, key = %s\n", key);
  } else {
    // Got valid return string.
    ret = reply->len;
    if (check_data) {
      int check_ret = CheckBuffer(reply->str, reply->len, key, nkey);
      if (check_ret != CHECK_OK) {
        ret = -1;
      }
    }
  }
  if (reply) {
    freeReplyObject(reply);
  }

#if CRASH_ON_FAILURE
  if (ret < 0) {
    assert(0);
  }
#endif

  return ret;
}

// Do "mget key1 key2 ..." to fetch multiple objs.
//
// Return:
//    < 0 : failure.
//    >= 0:  number of read hits.
static int ReadObjs(redisContext *redis_ctx,
                    std::vector<char*> &keys,
                    std::vector<int> &keys_len,
                    std::vector<int> &ret_sizes) {
  if (keys.size() == 0) {
    return 0;
  }

  if (keys.size() == 1) {
    int ret = ReadObj(redis_ctx, keys[0], keys_len[0], NULL, 0);
    if (ret > 0) {
      ret_sizes.push_back(ret);
      return 1;
    } else {
      return ret;
    }
  }

  // Prepare mget cmd.
  string format = "mget";

  for (int i = 0; i < keys.size(); i++) {
    format.append(" ");
    format.append(keys[i], keys_len[i]);
  }

  // Issue request.
  redisReply *reply = (redisReply *)redisCommand(redis_ctx, format.c_str());

  // Parse reply.
  int ret = -1;
  int check_ret = CHECK_OK;
  if (!reply) {
    // cmd failed.
  } else if (reply->type != REDIS_REPLY_ARRAY || reply->elements < 1) {
    // no data returned.
  } else {
    ret = 0;
    for (int i = 0; i < reply->elements; i++) {
      int data_size = 0;
      if (!reply->element[i]) {
        // No data for element i.
        ret_sizes.push_back(0);
      } else if (reply->element[i]->type == REDIS_REPLY_STRING) {
        // Got a valid string.
        if (check_data) {
          check_ret = CheckBuffer(reply->element[i]->str,
                                  reply->element[i]->len,
                                  keys[i],
                                  keys_len[i]);
        } else {
          check_ret = CHECK_OK;
        }
        if (check_ret == CHECK_OK) {
          ret_sizes.push_back(reply->element[i]->len);
          ret++;
        } else {
          ret = check_ret;
          break;
        }
      } else {
        // A miss.
        ret_sizes.push_back(0);
      }
    }
  }

  if (reply) {
    freeReplyObject(reply);
  }
  return ret;
}

// Write (k, v) to Redis.
//
// Upon success, returns nbytes written.
// Return -1 otherwise.
static int WriteObj(redisContext* redis_ctx,
                    char* key,
                    int nkey,
                    char* data,
                    int nbytes,
                    uint32_t exptime) {
  redisReply *reply =
    (redisReply *)redisCommand(redis_ctx, "SET %b %b",
                               key, nkey, data, nbytes);
  int ret = -1;
  if (!reply) {
    // Failure.
  } else if (strncmp(reply->str, "OK", 2) != 0) {
    // Upon success, reply->type == REDIS_REPLY_STATUS,
    // reply->str is "OK".
  } else {
    ret = nbytes;
  }
  if (reply) {
    freeReplyObject(reply);
  }
  return ret;
}

static void TryRedisCmd(redisContext* ctx) {
  char buf[10000];
  char key1[100];
  char key2[100];
  char key3[100];

  sprintf(key1, "key1");
  sprintf(key2, "key2");
  sprintf(key3, "no-key3");

  std::vector<char*> keys;
  std::vector<int> keys_len;
  std::vector<int> ret_sizes;
  keys.push_back(key1);
  // Length includes the trailing null.
  keys_len.push_back(strlen(key1));
  keys.push_back(key2);
  keys_len.push_back(strlen(key2));
  keys.push_back(key3);
  keys_len.push_back(strlen(key3));
  ReadObjs(ctx, keys, keys_len, ret_sizes);

  ReadObj(ctx, key1, strlen(key1), NULL, 0);
  ReadObj(ctx, key3, strlen(key3), NULL, 0);
}


static void Worker(TaskContext* task) {

  int bufsize = MAX_OBJ_SIZE;
  char key[200];
  char buf[bufsize];
  unsigned long tBeginUsec;

  long elapsedMicroSec;

  if (task->id == 0) {
    printf("workload started, will run %ld seconds\n",
           task->runTimeSeconds);
  }

  int ret;

  unsigned long kid, t1, t2, opcnt = 0;
  unsigned long tBeginSec = NowInSec();
  tBeginUsec = NowInUsec();
  int obj_size = 0;
  bool last_write_failed = false;

  vector<char*> keys;
  vector<int> keys_len;
  vector<int> ret_sizes;
  for (int i = 0; i < mget_batch_size; i++) {
    keys.push_back((char*)malloc(200));
  }

  while (NowInSec() - tBeginSec < task->runTimeSeconds) {
    if (task->doWrite) {
      // This task performs write ops.
      if (task->writeOps > 0) {
        ThrottleForQPS(task->writeTargetQPS, tBeginUsec, task->writeOps);
      }

      if (!last_write_failed) {
        kid = NextObjectID();
        // key starts with proc-id.
        sprintf(key, "%d-key-%ld", procid, kid);
        obj_size = ioSizes[kid % ioSizes.size()];
        if (randomize_data) {
          arc4random_buf(buf, obj_size);
        } else {
          memset(buf, kid, obj_size);
        }
        if (check_data) {
          EncodeBuffer(buf, obj_size, key, strlen(key));
        }
      } else {
        // if last write is a failure, retry write using the same key
      }

      t1 = NowInUsec();
      ret = WriteObj(task->redis_ctx,
                     key,
                     strlen(key),
                     buf,
                     obj_size,
                     expire_time);
      t2 = NowInUsec() - t1;

      if (task->writeRec) {
        task->writeRec->Add(t2);
      }
      if (histogram_write) {
        // There is only one writer, so no lock needed.
        hdr_record_value(histogram_write, t2);
      }
      if (t2 > 30000) {
        printf("write key %s: costs %ld ms\n", key, t2 / 1000);
      }

      task->writeOps++;
      if (ret < 0 ) {
        task->writeFailure++;
        last_write_failed = true;
      } else {
        task->writeSuccess++;
        task->writeBytes += obj_size;
        last_write_failed = false;
      }
      opcnt++;

    } else {
      // This task performs read ops.
      if (task->readOps > 0) {
        ThrottleForQPS(task->readTargetQPS, tBeginUsec, task->readOps);
      }
      keys_len.clear();
      ret_sizes.clear();
      for (int i = 0; i < mget_batch_size; i++) {
        kid = GetRandomID();
        sprintf(keys[i], "%d-key-%ld", procid, kid);
        keys_len.push_back((int)strlen(keys[i]));
      }

      t1 = NowInUsec();
      ret = ReadObjs(task->redis_ctx, keys, keys_len, ret_sizes);
      t2 = NowInUsec() - t1;

      if (task->readRec) {
        task->readRec->Add(t2);
      }
      if (histogram_read) {
        pthread_mutex_lock(&stats_lock);
        hdr_record_value(histogram_read, t2);
        pthread_mutex_unlock(&stats_lock);
      }
      if (t2 > 30000) {
        dbg("read key %s: costs %ld ms\n", key, t2 / 1000);
      }

      // Each obj counts as one read op.
      task->readOps += mget_batch_size;
      opcnt += mget_batch_size;
      task->readObjs += mget_batch_size;

      if (ret >= 0) {
        task->readSuccess += mget_batch_size;
        task->readHit += ret;
        task->readMiss += (mget_batch_size - ret);

        for (int i : ret_sizes) {
          task->readBytes += i;
        }
      } else {
        task->readFailure += mget_batch_size;
        // Redis access failure. Close connection and re-open it with a larger
        // timeout value.
        redisFree(task->redis_ctx);
        err("proc %d thread %d (redis srv %s:%d) redis failure, reconnect...\n",
            procid, task->id, redis_server_ip, redis_server_port);
        struct timeval timeout = {15, 500000}; // 15 seconds
        task->redis_ctx = redisConnectWithTimeout(redis_server_ip,
                                                  redis_server_port,
                                                  timeout);
        assert(task->redis_ctx != NULL);
        task->reconnects++;
      }

    }

    if (opcnt % 50000000 == 0) {
      printf("workload running...\n");
      dbg("task %d: read %ld, write %ld: read-success %ld, read-miss %ld, "
             "read-fail %ld, write-success %ld, write-fail %ld\n",
             task->id, task->readOps, task->writeOps,
             task->readSuccess, task->readMiss, task->readFailure,
             task->writeSuccess, task->writeFailure);
    }
  }

  for (int i = 0; i < mget_batch_size; i++) {
    free(keys[i]);
    keys[i] = NULL;
  }

  if (task->id == 0) {
    printf("io thread %d finished, %ld reads, %ld writes ...\n",
           task->id, task->readOps, task->writeOps);
  }
}



void help() {
  printf("Test hybrid-memory cache performance\n");
  printf("parameters: \n");
  printf("-a <ip>              : Redis server ip. Must provide.\n");
  printf("-p <port>            : Redis server port. Must provide.\n");
  printf("-s <obj sizes>       : \",\" separated object sizes. Def = 4000\n");
  printf("-n <num of objs>     : Populate this many objects before test. Def = 1000\n");
  printf("-t <num of threads>  : number of threads to run. At most 1 thread\n"
         "                       will run write worload. Def = 1\n");
  printf("-r <read QPS>        : Total read target QPS by all read threads.\n"
         "                       Def = 1000\n");
  printf("-w <write QPS>       : Total write target QPS. Default = 1000\n");
  printf("-i <seconds>         : Run workoad for these many seconds. Default = 10\n");
  //printf("-l                   : record per-request latency. Default false.\n");
  printf("-o                   : overwrite all data at beginning. Default not.\n");
  printf("-e                   : obj expire time in seconds. Default is 0\n"
         "                       (no epire)\n");
  printf("-x                   : After loading from disk also check data integrity.\n"
         "                       Default not.\n");
  printf("-m <num of keys>     : Perform mget of multiple keys. Default = 1 (regular get).\n");
  printf("-h                   : this message\n");
}



void TimerCallback(union sigval sv) {
  TimerContext *tc = (TimerContext*)sv.sival_ptr;

  TaskContext *tasks = tc->tasks;
  int ntasks = tc->ntasks;
  //printf("in timer callback: %d tasks, task ctx %p\n", ntasks, tasks);

  uint64_t reads = 0, writes = 0, read_failure = 0, write_failure = 0;
  uint64_t read_size = 0, write_size = 0;
  uint64_t read_miss = 0;

  OpStats last_stats;
  memcpy(&last_stats, &tc->stats, sizeof(OpStats));
  memset(&tc->stats, 0, sizeof(OpStats));

  for (int i = 0; i < ntasks; i++) {
    tc->stats.reads += tasks[i].readOps;
    tc->stats.read_size += tasks[i].readBytes;
    tc->stats.read_failure += tasks[i].readFailure;
    tc->stats.read_miss += tasks[i].readMiss;


    tc->stats.writes += tasks[i].writeOps;
    tc->stats.write_size  += tasks[i].writeBytes;
    tc->stats.write_failure  += tasks[i].writeFailure;
  }

  printf("In past %d seconds:  %ld reads (%ld failure, %ld miss), "
         "%ld writes (%ld failure), latest write # %ld\n",
         timer_cycle_sec,
         tc->stats.reads - last_stats.reads,
         tc->stats.read_failure - last_stats.read_failure,
         tc->stats.read_miss - last_stats.read_miss,
         tc->stats.writes - last_stats.writes,
         tc->stats.write_failure - last_stats.write_failure,
         NumberOfObjs());

}

int main(int argc, char** argv) {
  if (argc == 1) {
    help();
    return 0;
  }

  int c;
  vector<char*> dirPaths;
  vector<char*> dirSizesInStr;
  size_t maxItemHeaderMem = 0, maxItemDataMem = 0;
  bool record_latency = false;
  size_t chunk_size = 1024 * 1024;

  while ((c = getopt(argc, argv, "k:v:c:p:s:n:t:r:w:a:d:i:e:m:hlox")) != EOF) {
    switch(c) {
      case 'h':
        help();
        return 0;
      case 'o':
        overwrite_all = true;
        printf("will overwrite all data at beginning\n");
        break;
      case 'l':
        record_latency = true;
        break;
      case 'x':
        printf("will verify data integrity after loading.\n");
        check_data = true;
        break;
      case 'e':
        expire_time = atoi(optarg);
        printf("obj expire time = %d sec\n", expire_time);
        break;
      case 'a':
        redis_server_ip = optarg;
        printf("Redis server at: %s\n", redis_server_ip);
        break;
      case 'p':
        redis_server_port = atoi(optarg);
        printf("Redis server port %d\n", redis_server_port);
        break;
      case 's':
        size_str = optarg;
        printf("will use sizes: %s\n", size_str.c_str());
        break;
      case 'n':
        initObjNumber = std::stoi(optarg);
        printf("will populate %ld objs before test\n", initObjNumber);
        break;
      case 'm':
        mget_batch_size = std::stoi(optarg);
        printf("mget to fetch %d objs per batch\n", mget_batch_size);
        break;
      case 'i':
        runTimeSeconds = std::stoi(optarg);
        printf("will run workload for %ld seconds\n", runTimeSeconds);
        break;
      case 'r':
        readTargetQPS = std::stoi(optarg);
        break;
      case 'w':
        writeTargetQPS = std::stoi(optarg);
        break;
      case 't':
        numTasks = atoi(optarg);
        printf("will use %d threads\n", numTasks);
        break;
      case '?':
        help();
        return 0;
      default:
        help();
        return 0;
    }
  }
  if (optind < argc) {
    help();
    return 0;
  }

  if (redis_server_port <= 0 || !redis_server_ip) {
    help();
    return 0;
  }

  // Split size string into sizes.
  vector<string> sizes = SplitString(size_str, ",");
  printf("will use %ld sizes\n", sizes.size());
  for (string s : sizes) {
    ioSizes.push_back(std::stoi(s));
    assert(ioSizes.back() <= MAX_OBJ_SIZE);
    printf("\t%ld\n", ioSizes.back());
  }
  if (ioSizes.size() < 1) {
    help();
    return 0;
  }

  int readTasks = 0, writeTasks = 0;
  if (writeTargetQPS > 0) {
    writeTasks = 1;
  }
  readTasks = numTasks - writeTasks;


  dbg("will run %d read tasks, %d write tasks\n",
      readTasks, writeTasks);

  int lowest = 1;
  int highest = 100000000;
  int sig_digits = 3;
  hdr_init(lowest, highest, sig_digits, &histogram_read);
  hdr_init(lowest, highest, sig_digits, &histogram_write);
  printf("memory footprint of hdr-histogram: %ld\n",
         hdr_get_memory_size(histogram_read));

  std::unique_ptr<Recorder<unsigned int>> readRec;
  std::unique_ptr<Recorder<unsigned int>> writeRec;
  if (record_latency) {
    size_t expectTotalReads= (size_t)(runTimeSeconds * readTargetQPS) + 1000000;
    size_t expectTotalWrites = (size_t)(runTimeSeconds * writeTargetQPS) + 1000000;

    // to record read/write latency in usec.
    readRec.reset(new Recorder<unsigned int>(expectTotalReads));
    memset(readRec->Elements(), 0, sizeof(int) * expectTotalReads);

    writeRec.reset(new Recorder<unsigned int>(expectTotalWrites));
    memset(writeRec->Elements(), 0, sizeof(int) * expectTotalWrites);
  }


  std::srand(NowInUsec());

  // Prepare contexts for all worker threads.
  TaskContext tasks[numTasks];
  struct timeval timeout = {10, 500000}; // max 10 seconds
  memset(tasks, 0, sizeof(TaskContext) * numTasks);

  for (int i = 0; i < numTasks; i++) {

    tasks[i].id = i;
    tasks[i].readRec = record_latency ? readRec.get() : NULL;
    tasks[i].writeRec = record_latency ? writeRec.get() : NULL;
    tasks[i].runTimeSeconds = runTimeSeconds;

    // Open a Redis connection.
    tasks[i].redis_ctx = redisConnectWithTimeout(redis_server_ip,
                                                 redis_server_port,
                                                 timeout);
    assert(tasks[i].redis_ctx != NULL);

    // Prepare read/write targets.
    if (i < writeTasks) {
      dbg("task %d is writer\n", i);
      tasks[i].doWrite = true;
      tasks[i].writeTargetQPS = writeTargetQPS / writeTasks;
    } else {
      dbg("task %d is reader\n", i);
      tasks[i].doWrite = false;
      tasks[i].readTargetQPS = readTargetQPS / readTasks;
    }
  }

  //TryRedisCmd(tasks[0].redis_ctx);

  procid = (uint32_t)redis_server_port;

  char key[200];
  char tmpbuf[MAX_OBJ_SIZE];
  uint32_t exptime = expire_time;

  // step 1: populate the store.
  if (overwrite_all) {
    printf("===== Step 1: populate data ...\n");
    for (size_t cnt = 0; cnt < initObjNumber; cnt++) {
      sprintf(key, "%d-key-%ld", procid, cnt);
      size_t objsize = ioSizes[cnt % ioSizes.size()];

      // TODO: scramble the buf, embed data-size and metadata.
      if (randomize_data) {
        arc4random_buf(tmpbuf, objsize);
      } else {
        memset(tmpbuf, cnt, objsize);
      }

      if (check_data) {
        EncodeBuffer(tmpbuf, objsize, key, strlen(key));
      }
      while (WriteObj(tasks[0].redis_ctx,
                      key,
                      strlen(key),
                      tmpbuf,
                      objsize,
                      expire_time) <= 0) {
        // Retry upon failures.
        usleep(1000);
      }

    }
  } else {
    printf("===== Step 1: won't write data to init ...\n");
  }

  //printf("\nsleep %d seconds to let objects expire...\n", exptime);
  //sleep(exptime);
  objectNumber.store(initObjNumber);

  // Step 2: start worker threads.
  std::thread  workers[numTasks];


  printf("\n===== Step 2: run %d worker threads...\n", readTasks + writeTasks);
  //dbg("\n===== Step 2: run %d reader, %d writer threads...\n",
  //       readTasks, writeTasks);

  timer_t timer;

  TimerContext tctx;
  memset(&tctx, 0, sizeof(tctx));
  tctx.tasks = tasks;
  tctx.ntasks = numTasks;
  tctx.timer = &timer;

  CreateTimer(&timer, timer_cycle_sec * 1000, TimerCallback, &tctx);

  unsigned long t1 = NowInUsec();
  for (int i = 0; i < numTasks; i++) {
    workers[i] = std::thread(Worker, tasks + i);
  }

  size_t readBytes = 0, writeBytes = 0;
  size_t readFail = 0, readMiss = 0;
  size_t totalOps = 0, writeFail = 0;
  size_t totalReadOps = 0, totalWriteOps = 0;
  for (int i = 0; i < numTasks; i++) {
    if (workers[i].joinable()) {
      workers[i].join();
      dbg("joined thread %d\n", i);
    }
    readBytes += tasks[i].readBytes;
    writeBytes += tasks[i].writeBytes;
    readMiss += tasks[i].readMiss;
    readFail += tasks[i].readFailure;
    writeFail += tasks[i].writeFailure;
    totalOps += tasks[i].readOps;
    totalReadOps += tasks[i].readOps;
    totalOps += tasks[i].writeOps;
    totalWriteOps += tasks[i].writeOps;
  }
  double totalSecs = (NowInUsec() - t1) / 1000000.0;

  DeleteTimer(&timer);

  // Close redis connections.
  for (int i = 0; i < numTasks; i++) {
    if (tasks[i].redis_ctx) {
      redisFree(tasks[i].redis_ctx);
      tasks[i].redis_ctx = NULL;
    }
  }

  printf("\nIn total:  %ld ops in %f sec (%ld read, %ld write).\n"
         "Total IOPS = %.f, read IOPS %.f, write IOPS %.f\n"
         "Bandwidth = %.3f MB/s, read bw %.3f MB/s, write bw %.3f MB/s\n"
         "Read miss %ld (%.2f%%), read failure %ld, write failure %ld\n",
         //readRec->NumberElements() +  writeRec->NumberElements(),
         totalOps,
         totalSecs, totalReadOps, totalWriteOps,
         totalOps / totalSecs,
         totalReadOps / totalSecs, totalWriteOps / totalSecs,
         (readBytes + writeBytes) / totalSecs / 1000000,
         readBytes / totalSecs / 1000000,
         writeBytes / totalSecs / 1000000,
         readMiss,
         (readMiss + 0.0) / totalReadOps * 100,
         readFail,
         writeFail);

  PrintHistoStats(histogram_read, "\n============== Read latency in ms");
  PrintHistoStats(histogram_write, "\n============== Write latency in ms");

  if (record_latency) {
    readRec->Sort();
    writeRec->Sort();
    PrintStats(readRec->Elements(), readRec->NumberElements(),
               histogram_read,
               "\n============== Read latency in ms");
    PrintStats(writeRec->Elements(), writeRec->NumberElements(),
               histogram_write,
               "\n============== Write latency in ms");
  }
  return 0;
}

static void PrintHistoStats(struct hdr_histogram *histogram,
                            const char *header) {
  int hist_min = hdr_value_at_percentile(histogram, 0);
  int hist_max = (int)hdr_max(histogram);
  int hist_p10 = hdr_value_at_percentile(histogram, 10);
  int hist_p20 = hdr_value_at_percentile(histogram, 20);
  int hist_p50 = hdr_value_at_percentile(histogram, 50);
  int hist_p90 = hdr_value_at_percentile(histogram, 90);
  int hist_p95 = hdr_value_at_percentile(histogram, 95);
  int hist_p99 = hdr_value_at_percentile(histogram, 99);
  int hist_p999 = hdr_value_at_percentile(histogram, 99.9);

  cout << header << endl;
  cout << setw(12) << "min"
       << setw(12) << "10 %"
       << setw(12) << "20 %"
       << setw(12) << "50 %"
       << setw(12) << "90 %"
       << setw(12) << "95 %"
       << setw(12) << "99 %"
       << setw(12) << "99.9 %"
       << setw(12) << "max" << endl;
  cout << setw(12) << hist_min / 1000.0
       << setw(12) << hist_p10 / 1000.0
       << setw(12) << hist_p20 / 1000.0
       << setw(12) << hist_p50 / 1000.0
       << setw(12) << hist_p90 / 1000.0
       << setw(12) << hist_p95 / 1000.0
       << setw(12) << hist_p99 / 1000.0
       << setw(12) << hist_p999 / 1000.0
       << setw(12) << hist_max / 1000.0 << endl;
}

static void PrintStats(unsigned int *latency,
                       int size,
                       struct hdr_histogram *histogram,
                       const char *header) {
  int lat_min = latency[0];
  int lat_10 = latency[(int)(size * 0.1)];
  int lat_20 = latency[(int)(size * 0.2)];
  int lat_50 = latency[(int)(size * 0.5)];
  int lat_90 = latency[(int)(size * 0.9)];
  int lat_95 = latency[(int)(size * 0.95)];
  int lat_99 = latency[(int)(size * 0.99)];
  int lat_999 = latency[(int)(size * 0.999)];
  int lat_max = latency[size - 1];
  int hist_min = (int)hdr_min(histogram);
  int hist_max = (int)hdr_max(histogram);
  int hist_p10 = hdr_value_at_percentile(histogram, 10);
  int hist_p20 = hdr_value_at_percentile(histogram, 20);
  int hist_p50 = hdr_value_at_percentile(histogram, 50);
  int hist_p90 = hdr_value_at_percentile(histogram, 90);
  int hist_p95 = hdr_value_at_percentile(histogram, 95);
  int hist_p99 = hdr_value_at_percentile(histogram, 99);
  int hist_p999 = hdr_value_at_percentile(histogram, 99.9);

  cout << header << endl;
  cout << setw(12) << "min"
       << setw(12) << "10 %"
       << setw(12) << "20 %"
       << setw(12) << "50 %"
       << setw(12) << "90 %"
       << setw(12) << "95 %"
       << setw(12) << "99 %"
       << setw(12) << "99.9 %"
       << setw(12) << "max" << endl;
  cout << setw(12) << lat_min / 1000.0
       << setw(12) << lat_10 / 1000.0
       << setw(12) << lat_20 / 1000.0
       << setw(12) << lat_50 / 1000.0
       << setw(12) << lat_90 / 1000.0
       << setw(12) << lat_95 / 1000.0
       << setw(12) << lat_99 / 1000.0
       << setw(12) << lat_999 / 1000.0
       << setw(12) << lat_max / 1000.0 << endl;
  cout << setw(12) << hist_min / 1000.0
       << setw(12) << hist_p10 / 1000.0
       << setw(12) << hist_p20 / 1000.0
       << setw(12) << hist_p50 / 1000.0
       << setw(12) << hist_p90 / 1000.0
       << setw(12) << hist_p95 / 1000.0
       << setw(12) << hist_p99 / 1000.0
       << setw(12) << hist_p999 / 1000.0
       << setw(12) << hist_max / 1000.0 << endl;
}

