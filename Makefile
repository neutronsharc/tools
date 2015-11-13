
GCC = gcc -std=c99

ZK_CFLAGS := -I/usr/local/include/zookeeper -DTHREADED
ZK_LDFLAGS := -L/usr/local/lib -lzookeeper_mt

#REDIS_CFLAGS := -I/usr/include/hiredis
#REDIS_LDFLAGS := -L/usr/lib/x86_64-linux-gnu/ -lhiredis

CFLAGS := -g $(ZK_CFLAGS) -I/usr/include $(REDIS_CFLAGS)
#LDFLAGS := $(ZK_LDFLAGS) -ljansson $(REDIS_LDFLAGS)
LDFLAGS := $(ZK_LDFLAGS) $(REDIS_LDFLAGS)


.PHONY : all clean

all : zktest jsontest

jsontest : json.o parson.o
	$(GCC) $^ $(LDFLAGS) -o $@

zktest : zktest.o
	$(GCC) $^ $(LDFLAGS) -o $@

redis_test : redis_test.o
	$(GCC) $^ $(LDFLAGS) -o $@

%.o : %.c
	$(GCC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) *.o $(all)
