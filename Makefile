CFLAGS=-std=c++11 -Wall
INCLUDES= 
BINS=client_test1 client_test2 server_test
BASEOBJS=neusc_server.o neusc_clientsync.o 
CC=g++
LIBS=-lpthread
Q=

all: $(BINS) $(BASEOBJS)

$(BINS): $(BINS:%=%.o) 

%.o: %.cc $(BASEOBJS:%.o=%.h)
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%: %.o $(BASEOBJS) 
	$(Q)$(CC) -o $@ $< $(BASEOBJS) $(LIBS)

clean: 
	$(Q)rm -rf $(BINS)
	$(Q)rm -rf *.o



