
ifdef CONFIG_USER_NDSO
EXEC = ndso
endif

ifdef CONFIG_USER_AWG
EXEC = awg
endif

ifdef CONFIG_USER_PPIFPGACGI
EXEC = t_httpd
endif

OBJS = thttpd.o libhttpd.o mmc.o timers.o match.o tdate_parse.o


CFLAGS += -DHAVE_FCNTL_H=1 -DEMBED=1 -DHAVE_MEMORY_H=1 \
	-DHAVE_PATHS_H=1 -DTIME_WITH_SYS_TIME=1 -DHAVE_DIRENT_H=1 \
	-DHAVE_LIBCRYPT=1 -DHAVE_STRERROR=1 -DHAVE_WAITPID=1 \
	-DHAVE_UNISTD_H=1 -DHAVE_GETPAGESIZE=1 -O2
FLTFLAGS = -s 0x8000

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

romfs:
	$(ROMFSINST) /bin/$(EXEC)
	$(ROMFSINST) -e CONFIG_USER_PPIFPGACGI -a "t_httpd &" /etc/rc

clean:
	-rm -f $(EXEC) *.elf *.gdb *.o ndso awg

libhttpd.o: mime_encodings.h mime_types.h

mime_encodings.h: mime_encodings.txt
	rm -f mime_encodings.h
	sed < mime_encodings.txt > mime_encodings.h \
	  -e 's/#.*//' -e 's/[ 	]*$$//' -e '/^$$/d' \
	  -e 's/[ 	][ 	]*/", "/' -e 's/^/{ "/' -e 's/$$/" },/'

mime_types.h: mime_types.txt
	rm -f mime_types.h
	sed < mime_types.txt > mime_types.h \
	  -e 's/#.*//' -e 's/[ 	]*$$//' -e '/^$$/d' \
	  -e 's/[ 	][ 	]*/", "/' -e 's/^/{ "/' -e 's/$$/" },/'

thttpd.o: config.h version.h libhttpd.h mmc.h timers.h match.h

libhttpd.o: config.h version.h libhttpd.h mime_encodings.h mime_types.h \
		mmc.h timers.h match.h tdate_parse.h

mmc.o: mmc.h

timers.o: timers.h

match.o: match.h

tdate_parse.o: tdate_parse.h

