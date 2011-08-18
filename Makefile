OBJS = penn_daq.o ttot.o xl3_util.o net_util.o xl3_rw.o mtc_rw.o db.o mtc_util.o mtc_init.o crate_init.o fec_util.o ped_run.o crate_cbal.o cgt_test_1.o cmos_m_gtvalid.o cald_test.o readout_test.o chinj_test.o final_test.o disc_check.o pouch.o json.o
SRCS = $(OBJS:.o=.c)

CC = gcc

LIBS = -lm -lcurl

all: penn_daq tut vwr

penn_daq: $(OBJS)
	python ./penn_daq_gen.py
	gcc -o $@ $(OBJS) $(LIBS) -g

tut:
	python ./tut_gen.py
	gcc -lreadline -lncurses -o tut tut.c

vwr:
	gcc -lreadline -lncurses -o vwr vwr.c

clean: 
	-$(RM) core *.o

