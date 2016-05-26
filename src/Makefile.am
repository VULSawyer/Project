bin_PROGRAMS = EES4100_Bridge


modbus_client_CFLAGS = $(AM_CFLAGS) $(MODBUS_CFLAGS)
modbus_client_LDADD = $(AM_LIBS) $(MODBUS_LIBS)

EES4100_Bridge_SOURCES = EES4100_Bridge.c
EES4100_Bridge_CFLAGS = $(AM_CFLAGS) $(BACNET_CFLAGS) $(MODBUS_CFLAGS)
EES4100_Bridge_LDADD = $(AM_LIBS) $(BACNET_LIBS) $(MODBUS_LIBS)

BACNET_DEPENDENT_SRC = EES4100_Bridge.c

BUILT_SOURCES = bacnet_namespace.h .bacnet_dependent_src_stamp

EXTRA_DIST = bacnet_api_names

.bacnet_dependent_src_stamp: bacnet_api_names $(BACNET_DEPENDENT_SRC)
	cat $< | $(top_srcdir)/scripts/check_namespace.sh $< $?
	touch $@

bacnet_namespace.h: bacnet_api_names
	cat $< | $(top_srcdir)/scripts/build_namespace.sh > $@
