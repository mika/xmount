##Process this file with automake to create Makefile.in
bin_PROGRAMS = xmount
man_MANS = xmount.1
xmount_SOURCES = md5.c xmount.c
xmount_CFLAGS = -fno-strict-aliasing -D_LARGEFILE64_SOURCE $(fuse_CFLAGS)
LIBS += $(fuse_LIBS)
# Link against the not installed static libaff and libewf
xmount_LDADD = libaff/lib/.libs/libafflib.a libewf/libewf/.libs/libewf.a -lstdc++
