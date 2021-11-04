CC=cc
CFLAGS=-I. -Wall
#CFLAGS=-I. -Wall 

ifeq ($(MACHINE),m32)
	CFLAGS+=-m32
else
ifeq ($(MACHINE),m64)
	CFLAGS+=-m64
endif
endif

ifeq ($(REL),debug)
	CFLAGS+=-g -O0 -DDEBUG 
else
ifeq ($(REL),test)
	CFLAGS+=-g
else
	CFLAGS+=-O3 
endif
endif

DEPS = dre.h Makefile
TGT_LIB= libdre.a
TGT_LIBOBJ = dre.o crc64.o log.o
BIN= compress decompress comp_edge decomp_edge \
     comp_edge1 comp_edge2 decomp_edge1 decomp_edge2 \
     comp_smallbuff comp_loadcache decomp_loadcache decomp_fragblock \
     compress_rnd cache_info comp_desynch comp_perf

COMPRESS_ARGS= compress.c drec.o crc64.o log.o
DECOMPRESS_ARGS= decompress.c dred.o crc64.o log.o

all: $(TGT_LIB) $(BIN)

$(TGT_LIB): $(TGT_LIBOBJ)
	$(AR) rv $@ $(TGT_LIBOBJ)

log.o: log.c log.h
	$(CC) $(CFLAGS) -c -o $@ $<

drec.o: dre.c
	$(CC) $(CFLAGS) -DDRE_COMP -c -o $@ $<

dred.o: dre.c
	$(CC) $(CFLAGS) -c -o $@ $<

compress: $(COMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS)

compress_rnd:  $(COMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS) -DTEST_RND_FLUSH -DTEST_RND_RD_SZ

decompress: $(DECOMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS)

comp_edge:  $(COMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS) -DTEST_EDGE

comp_edge1:  $(COMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS) -DTEST_EDGE1

comp_edge2:  $(COMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS) -DTEST_EDGE2

decomp_edge: $(DECOMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS) -DTEST_EDGE

decomp_edge1: $(DECOMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS) -DTEST_EDGE1

decomp_edge2: $(DECOMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS) -DTEST_EDGE2

comp_smallbuff: $(COMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS) -DTEST_SMALL_BUFF

comp_desynch: $(COMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS) -DTEST_DESYNCHRO

comp_loadcache: $(COMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS) -DTEST_LOAD_CACHE

decomp_loadcache: $(DECOMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS) -DTEST_LOAD_CACHE

decomp_fragblock: $(DECOMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS) -DTEST_FRAG_BLK

comp_perf: $(COMPRESS_ARGS)
	$(CC) -o $@ $^ $(CFLAGS) -DTEST_PERF

cache_info: cache_info.c $(TGT_LIB)
	$(CC) -o $@ $^ $(CFLAGS)


.PHONY: clean

clean:
	rm -f *.o *~ $(BIN) $(TGT_LIB) $(TGT_LIBOBJ) comp.db comp.db.idx decomp.db.idx decomp.db

clobber:
	rm -f randomfile randomfile.unc 
