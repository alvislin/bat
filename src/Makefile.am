
AM_CFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = bat

bat_SOURCES = \
	bat.c \
	common.c \
	analyze.c \
	wav_play_record.c
	
if TINYALSA
bat_SOURCES += wav_play_record_tiny.c
endif
