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
#include <string>
#include <thread>
#include <typeinfo>
#include <hiredis.h>
#include <bsd/stdlib.h>

#include "debug.h"
#include "recorder.h"
#include "utils.h"

using namespace std;

#define CRASH_ON_FAILURE 0

#define MAX_OBJ_SIZE (5000000)

// Benchmark configs.
int numTasks = 1;
vector<size_t> ioSizes;
double writeRatio = 0;
size_t runTimeSeconds = 0;
size_t initObjNumber = 1000;
size_t readTargetQPS = 1000;
size_t writeTargetQPS = 1000;
size_t numberShards = 1;

bool use_user_provide_buf = false;
bool overwrite_all = false;
static bool check_data = false;
static bool randomize_data = true;

static int redis_server_port = -1;
static char *redis_server_ip = NULL;

static uint32_t procid = -1;

// By default don't use exptime.
uint32_t expire_time = 0;

static int timer_cycle_sec = 2;

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
  redisContext *rctx;

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
  unsigned long readBytes;

  // Target qps for read issued by this worker.
  unsigned long readTargetQPS;

  unsigned long readSuccess;
  unsigned long readFailure;
  unsigned long readMiss;

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


static void PrintStats(unsigned int *latency, int size, const char *header);

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

// Read (k, v) from Redis.
//
// Return value:
//   0: success,
//   1: GET failed
//   2: read miss
//   3: data size mismatch
//   4: data corruption
static int ReadObj(redisContext *rctx,
                   char* key,
                   int nkey,
                   char* valbuf,
                   int expectDataSize) {
  int ret = 0;

  redisReply *reply = (redisReply *)redisCommand(rctx, "GET %s", key);
  if (reply) {
    if (reply->str == NULL) {
      ret = 2;
      printf("read miss, key = %s\n", key);
    } else if (check_data) {
      if (reply->len != expectDataSize) {
        // length mismatch
        ret = 3;
      } else if (memcmp(valbuf, reply->str, reply->len) != 0) {
        // data corrupted
        ret = 4;
      }
    }

    freeReplyObject(reply);

  } else {
    // "GET" failed.
    ret = 1;
  }

#if CRASH_ON_FAILURE
  if (ret != 0) {
    assert(0);
  }
#endif

  return ret;
}

// Write (k, v) to Redis.
//
// Upon success, returns nbytes written.
// Return -1 otherwise.
static int WriteObj(redisContext* rctx,
                    char* key,
                    int nkey,
                    char* data,
                    int nbytes,
                    uint32_t exptime) {
  redisReply *reply = (redisReply *)redisCommand(rctx, "SET %b %b",
                                   key, nkey, data, nbytes);
  if (!reply) {
    //printf("Set obj %s failed\n", key);
    return -1;
  } else if (strncmp(reply->str, "OK", 2) != 0) {
    return -1;
  } else {
    freeReplyObject(reply);
    return nbytes;
  }
}


static void Worker(TaskContext* task) {

  int bufsize = MAX_OBJ_SIZE;
  char key[200];
  char buf[bufsize];
  char *user_provide_buf = NULL;
  unsigned long tBeginUsec;

  long elapsedMicroSec;

  if (task->id == 0) {
    printf("workload started, will run %ld seconds\n",
           task->runTimeSeconds);
  }

  // User preallocate a page-aligned buf. KV lib will perform zero-copy to
  // load data directly into this buf.
  // Use this API for better performance. Make sure you understand what
  // you are doing.
  if (use_user_provide_buf) {
    assert(posix_memalign((void**)&user_provide_buf, 4096, bufsize) == 0);
  }

  int ret;

  unsigned long kid, t1, t2, opcnt = 0;
  unsigned long tBeginSec = NowInSec();
  tBeginUsec = NowInUsec();
  int objSize = 0;
  bool last_write_failed = false;

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
        objSize = ioSizes[kid % ioSizes.size()];
        if (randomize_data) {
          arc4random_buf(buf, objSize);
        } else {
          memset(buf, kid, objSize);
        }

      } else {
        // if last write is a failure, retry write using the same key
      }

      t1 = NowInUsec();
      ret = WriteObj(task->rctx, key, strlen(key), buf, objSize, expire_time);
      t2 = NowInUsec() - t1;

      if (task->writeRec) {
        task->writeRec->Add(t2);
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
        task->writeBytes += objSize;
        last_write_failed = false;
      }
      opcnt++;

    } else {
      // This task performs read ops.
      if (task->readOps > 0) {
        ThrottleForQPS(task->readTargetQPS, tBeginUsec, task->readOps);
      }
      kid = GetRandomID();
      sprintf(key, "%d-key-%ld", procid, kid);
      objSize = ioSizes[kid % ioSizes.size()];
      memset(buf, kid, objSize);

      t1 = NowInUsec();
      ret = ReadObj(task->rctx,
                    key,
                    strlen(key),
                    buf,
                    objSize);
      t2 = NowInUsec() - t1;

      if (ret == 0 && task->readRec) {
        task->readRec->Add(t2);
      }
      if (t2 > 30000) {
        dbg("read key %s: costs %ld ms\n", key, t2 / 1000);
      }

      task->readOps++;
      opcnt++;
      switch (ret) {
        case 0:
          task->readSuccess++;
          task->readBytes += objSize;
          break;
        case 2:
          task->readMiss++;
          break;
        default:
          task->readFailure++;
          break;
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

  if (user_provide_buf) {
    free(user_provide_buf);
  }
  if (task->id == 0)
  //printf("workload finished...\n");
  printf("io thread %d finished, %ld reads, %ld writes ...\n",
         task->id, task->readOps, task->writeOps);
}



void help() {
  printf("Test hybrid-memory cache performance\n");
  printf("parameters: \n");
  printf("-a <ip>              : Redis server ip. Must provide.\n");
  printf("-p <port>            : Redis server port. Must provide.\n");
  printf("-s <obj sizes>       : \",\" separated object sizes. Must provide\n");
  printf("-n <num of objs>     : Populate this many objects before test. Def = 1000\n");
  printf("-t <num of threads>  : number of threads to run. At most 1 thread\n"
         "                       will run write worload. Def = 1\n");
  printf("-r <read QPS>        : Total read target QPS by all read threads.\n"
         "                       Def = 1000\n");
  printf("-w <write QPS>       : Total write target QPS. Default = 1000\n");
  printf("-i <seconds>         : Run workoad for these many seconds. Default = 10\n");
  printf("-l                   : record per-request latency. Default false.\n");
  printf("-o                   : overwrite all data at beginning. Default not.\n");
  printf("-e                   : obj expire time in seconds. Default is 0\n"
         "                       (no epire)\n");
  printf("-u                   : Pass user-preallocated buf to KV lib to \n"
         "                       perform zero-copy.\n");
  printf("-x                   : After loading from disk also check data integrity.\n"
         "                       Default not.\n");
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
  vector<char*> sizeStrs;
  vector<char*> dirPaths;
  vector<char*> dirSizesInStr;
  size_t maxItemHeaderMem = 0, maxItemDataMem = 0;
  bool record_latency = false;
  size_t chunk_size = 1024 * 1024;

  while ((c = getopt(argc, argv, "k:v:c:p:s:n:t:r:w:a:d:i:e:hluox")) != EOF) {
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
      case 'u':
        use_user_provide_buf = true;
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
        sizeStrs = SplitString(optarg, ",");
        printf("will use %ld sizes\n", sizeStrs.size());
        for (string s : sizeStrs) {
          ioSizes.push_back(std::stoi(s));
          printf("\t%ld", ioSizes.back());
          assert(ioSizes.back() <= MAX_OBJ_SIZE);
        }
        printf("\n");
        break;
      case 'n':
        initObjNumber = std::stoi(optarg);
        printf("will populate %ld objs before test\n", initObjNumber);
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
  if (ioSizes.size() < 1) {
    help();
    return 0;
  }
  if (redis_server_port <= 0 || !redis_server_ip) {
    help();
    return 0;
  }

  printf("will %s use pre-allocated buf for zero-copy\n",
         use_user_provide_buf ? "" : "NOT");

  int readTasks = 0, writeTasks = 0;
  if (writeTargetQPS > 0) {
    writeTasks = 1;
  }
  readTasks = numTasks - writeTasks;


  dbg("will run %d read tasks, %d write tasks\n",
      readTasks, writeTasks);

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
  struct timeval timeout = {1, 500000}; // 1.5 seconds
  memset(tasks, 0, sizeof(TaskContext) * numTasks);

  for (int i = 0; i < numTasks; i++) {

    tasks[i].id = i;
    tasks[i].readRec = record_latency ? readRec.get() : NULL;
    tasks[i].writeRec = record_latency ? writeRec.get() : NULL;
    tasks[i].runTimeSeconds = runTimeSeconds;

    // Open a Redis connection.
    tasks[i].rctx = redisConnectWithTimeout(redis_server_ip,
                                            redis_server_port,
                                            timeout);
    assert(tasks[i].rctx != NULL);

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


  procid = (uint32_t)getpid();

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

      WriteObj(tasks[0].rctx, key, strlen(key), tmpbuf, objsize, expire_time);
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

  if (record_latency) {
    readRec->Sort();
    writeRec->Sort();
    PrintStats(readRec->Elements(), readRec->NumberElements(),
               "\n============== Read latency in ms");
    PrintStats(writeRec->Elements(), writeRec->NumberElements(),
               "\n============== Write latency in ms");
  }
  return 0;
}

static void PrintStats(unsigned int *latency, int size, const char *header) {
  int lat_min = latency[0];
  int lat_10 = latency[(int)(size * 0.1)];
  int lat_20 = latency[(int)(size * 0.2)];
  int lat_50 = latency[(int)(size * 0.5)];
  int lat_90 = latency[(int)(size * 0.9)];
  int lat_95 = latency[(int)(size * 0.95)];
  int lat_99 = latency[(int)(size * 0.99)];
  int lat_999 = latency[(int)(size * 0.999)];
  int lat_max = latency[size - 1];
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
}

