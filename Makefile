
GCC = gcc -std=c99

GXX = g++ -std=c++0x

ZK_CFLAGS := -I/usr/local/include/zookeeper -DTHREADED
ZK_LDFLAGS := -L/usr/local/lib -lzookeeper_mt -lpthread

REDIS_CFLAGS := -I/usr/include/hiredis
REDIS_LDFLAGS := -L/usr/lib/x86_64-linux-gnu/ -lhiredis

CFLAGS := -g $(ZK_CFLAGS) -I/usr/include $(REDIS_CFLAGS)
LDFLAGS := $(ZK_LDFLAGS) $(REDIS_LDFLAGS) -lrt -lpthread


.PHONY : all clean

all : zktest jsontest redis_test redis_bench

jsontest : json.o parson.o
	$(GCC) $^ $(LDFLAGS) -o $@

zktest : zktest.o zkutil.o
	$(GCC) $^ $(LDFLAGS) -o $@

redis_test : redis_test.o
	$(GCC) $^ $(LDFLAGS) -o $@

redis_bench : redis_bench.cc utils.cc
	$(GXX) $^ $(CFLAGS) $(LDFLAGS) -o $@

%.o : %.c
	$(GCC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) *.o $(all)
