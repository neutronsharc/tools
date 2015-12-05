#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hiredis.h>

int main(int argc, char **argv) {
  unsigned int j;
  redisContext *ctx;
  redisReply *reply;

  // talk to twemproxy at localhost:22120
  const char *hostname = "localhost";
  int port = 8888;

  struct timeval timeout = { 1, 500000 }; // 1.5 seconds
  ctx = redisConnectWithTimeout(hostname, port, timeout);
  if (ctx == NULL || ctx->err) {
    if (ctx) {
      printf("Connection error: %s\n", ctx->errstr);
      redisFree(ctx);
    } else {
      printf("Connection error: can't allocate redis context\n");
    }
    exit(-1);
  }


  // PING server
  reply = redisCommand(ctx, "PING");
  printf("PING: %s\n", reply->str);
  freeReplyObject(reply);

  // Set a key
  reply = redisCommand(ctx, "SET %s %s", "foo1", "hello world");
  printf("SET: %s\n", reply->str);
  freeReplyObject(reply);

  // Get a key
  reply = redisCommand(ctx, "GET foo");
  printf("GET foo: %s\n", reply->str);
  freeReplyObject(reply);

  // Del a key
  reply = redisCommand(ctx, "Del foo");
  printf("Del foo: %s\n", reply->str);
  freeReplyObject(reply);

  reply = redisCommand(ctx, "INCR counter");
  printf("INCR counter: %lld\n", reply->integer);
  freeReplyObject(reply);
  reply = redisCommand(ctx, "INCR counter");
  printf("INCR counter: %lld\n", reply->integer);
  freeReplyObject(reply);

  // Create a list of numbers, from 0 to 9.
  reply = redisCommand(ctx, "DEL mylist");
  freeReplyObject(reply);
  int i = 0;

  for (i = 0; i < 10; i++) {
    char buf[64];
    snprintf(buf, 64, "element-%d", i);
    reply = redisCommand(ctx, "LPUSH mylist %s", buf);
    freeReplyObject(reply);
  }

  reply = redisCommand(ctx,"LRANGE mylist 0 -1");
  if (reply->type == REDIS_REPLY_ARRAY) {
    for (i = 0; i < reply->elements; i++) {
      printf("%u) %s\n", i, reply->element[i]->str);
    }
  }

  // disconnect and free context.
  redisFree(ctx);

  return 0;
}
