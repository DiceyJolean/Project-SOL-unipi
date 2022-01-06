CC			= gcc
AR          = ar
CFLAGS	    += -std=c99 -g
ARFLAGS     = rvs
INCLUDES	= -I.
LDFLAGS 	= -L.
#OPTFLAGS	= -O3
LIBS        = -lpthread

OBJSERVER	= filestorage.o
OBJCLIENT	= client.o

TARGETS		= filestorage\
	client


.PHONY: all test1 test2 test3 clean
.SUFFIXES: .c .h

%: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) -o $@ $< $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) -c -o $@ $<

all		: $(TARGETS)

filestorage: $(OBJSERVER) libQueue.a libIntQueue.a libAPI.a libStorage.a libHash.a
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $^ $(LIBS)

client: $(OBJCLIENT) libAPI.a libQueue.a libQueue.a
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $^ $(LIBS)

libStorage.a: storage.o storage.h
	$(AR) $(ARFLAGS) $@ $^

libAPI.a: api.o api.h
	$(AR) $(ARFLAGS) $@ $^

libQueue.a: queue.o queue.h
	$(AR) $(ARFLAGS) $@ $^

libHash.a: icl_hash.o icl_hash.h
	$(AR) $(ARFLAGS) $@ $^

libIntQueue.a: intqueue.o intqueue.h
	$(AR) $(ARFLAGS) $@ $^

clean	:
	\rm -f $(TARGETS) *.o *~ *.a *.log

test1	:
	chmod +x ./test1.sh && ./test1.sh
	chmod +x ./statistiche.sh && ./statistiche.sh stat.log

test2	:
	chmod +x ./test2.sh && ./test2.sh
	chmod +x ./statistiche.sh && ./statistiche.sh stat.log

test3	:
	chmod +x ./test3.sh && chmod +x ./client.sh && ./test3.sh
	chmod +x ./statistiche.sh && ./statistiche.sh stat.log
