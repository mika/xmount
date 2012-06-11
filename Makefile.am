##Process this file with automake to create Makefile.in
bin_PROGRAMS = xmount
man_MANS = xmount.1
xmount_SOURCES = md5.c xmount.c
xmount_CFLAGS = -fno-strict-aliasing -D_LARGEFILE64_SOURCE $(fuse_CFLAGS)
LIBS += $(fuse_LIBS)
# Link against the not installed static libaff and libewf
#xmount_LDADD = libaff/lib/.libs/libafflib.a libewf/libewf/.libs/libewf.a /usr/lib/libssl.a /usr/lib/libcrypto.a -lstdc++ -lz
#xmount_CFLAGS += -Ilibewf/include
# To build on OSX using osxfuse
#xmount_LDADD = libaff/lib/.libs/libafflib.a libewf/libewf/.libs/libewf.a -lstdc++ -lz -losxfuse
#xmount_CFLAGS += -I/usr/local/include/osxfuse/ -D_FILE_OFFSET_BITS=64