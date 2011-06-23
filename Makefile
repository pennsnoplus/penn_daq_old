OBJS = penn_daq.o ttot.o xl3_util.o net_util.o xl3_rw.o mtc_rw.o db.o mtc_util.o mtc_init.o crate_init.o fec_util.o ped_run.o crate_cbal.o cgt_test_1.o cmos_m_gtvalid.o cald_test.o readout_test.o chinj_test.o final_test.o disc_check.o
SRCS = $(OBJS:.o=.c)

CC = gcc

LIBS = -lpillowtalk -lm

penn_daq: $(OBJS)
#	gcc $(CFLAGS) -c -o penn_daq.o penn_daq.c
	gcc -o $@ $(OBJS) $(LIBS)

tut:
	python tut_gen.py
	gcc -lreadline -lncurses -o tut tut.c

vwr:
	gcc -lreadline -lncurses -o vwr vwr.c

clean: 
	-$(RM) core *.o penn_daq

depend: $(SRCS)
	gcc -M $(SRCS) > .depend
	touch depend

include .depend
