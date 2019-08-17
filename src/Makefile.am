AM_CFLAGS = -Werror -Wall -Wextra -Wno-unused-parameter
# todo: add -pedantic later, allowing the draft to be relatively messy
# for now

bin_PROGRAMS = wwwlite

wwwlite_SOURCES = main.c inlinebox.c documentbox.c blockbox.c tablebox.c browserbox.c
noinst_HEADERS = 	 inlinebox.h documentbox.h blockbox.h tablebox.h browserbox.h
wwwlite_CFLAGS = $(LIBSOUP_CFLAGS) $(LIBXML_CFLAGS) $(GTK3_CFLAGS) $(AM_CFLAGS)
wwwlite_LDADD = $(LIBSOUP_LIBS) $(LIBXML_LIBS) $(GTK3_LIBS)
