#
# makefile
# Fan Liang, 2015-10-12 19:14
#

GEN_SRC := connection.cpp connection_manager.cpp mime_types.cpp reply.cpp request_handler.cpp request_parser.cpp server.cpp

GEN_OBJ := $(patsubst %.cpp,%.o, $(GEN_SRC))
LIBRARY := libsimpleasiohttp.a

LOCAL_DIR := /usr/local/include
LOCAL_LIB := /usr/local/lib

PRE_CXXFLAGS :=	-std=c++11 -g -Wall
CXXFLAGS := $(PRE_CXXFLAGS) 
LDFLAGS :=

INC := $(CXXFLAGS) $(LDFLAGS) -I$(LOCAL_DIR) -L$(LOCAL_LIB)

.PHONY: all clean

all: main $(LIBRARY)

%.o: %.cpp
	$(CXX) $(INC) -c $< -o $@ -lboost_system -lboost_filesystem -pthread

main: $(GEN_OBJ)
	$(CXX) -g $^ -o $@ main.cpp $(INC) -lboost_system -lboost_filesystem -pthread -lboost_serialization

$(LIBRARY) : $(GEN_OBJ)
	ar crvs $@ $^ 

clean:
	$(RM) *.o 
# vim:ft:=make
#
