bin_PROGRAMS = bacnet_server


#modbus_client_CFLAGS = $(AM_CFLAGS) $(MODBUS_CFLAGS)
#modbus_client_LDADD = $(AM_LIBS) $(MODBUS_LIBS)

bacnet_server_SOURCES = bacnet_server.c
bacnet_server_CFLAGS = $(AM_CFLAGS) $(BACNET_CFLAGS) $(MODBUS_CFLAGS)
bacnet_server_LDADD = $(AM_LIBS) $(BACNET_LIBS) $(MODBUS_LIBS)

BACNET_DEPENDENT_SRC = bacnet_server.c

BUILT_SOURCES = bacnet_namespace.h .bacnet_dependent_src_stamp

EXTRA_DIST = bacnet_api_names

.bacnet_dependent_src_stamp: bacnet_api_names $(BACNET_DEPENDENT_SRC)
	cat $< | $(top_srcdir)/scripts/check_namespace.sh $< $?
	touch $@

bacnet_namespace.h: bacnet_api_names
	cat $< | $(top_srcdir)/scripts/build_namespace.sh > $@
