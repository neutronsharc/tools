
#GCC = gcc -std=c99
GCC = gcc -g

GXX = g++ -std=c++0x -g

ZK_CFLAGS := -I/usr/local/include/zookeeper -DTHREADED
ZK_LDFLAGS := -L/usr/local/lib -lzookeeper_mt -lpthread

REDIS_CFLAGS := -I/usr/include/hiredis
REDIS_LDFLAGS := -L/usr/lib/x86_64-linux-gnu/ -lhiredis

CFLAGS := -g -I. -I/usr/include
LDFLAGS := -lrt -lpthread

objs = hdr_encoding.o hdr_histogram.o hdr_histogram_log.o \
       hdr_interval_recorder.o hdr_thread.o hdr_time.o    \
       hdr_writer_reader_phaser.o

HistogramLib = lib_hdr_histogram.a

.PHONY : all clean

all : $(HistogramLib)

$(HistogramLib) : $(objs)
	ar rcs $@ $^

%.o : %.c
	$(GCC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) *.o $(objs) $(HistogramLib)
