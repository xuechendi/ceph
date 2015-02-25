=========================
 Tracing Ceph With BlkKin
=========================

Ceph can use Blkin, a library created by Marios Kogias and others,
which enables tracking a specific request from the time it enters
the system at higher levels till it is finally served by RADOS.

In general, Blkin implements the Dapper_ tracing semantics
in order to show the causal relationships between the different
processing phases that an IO request may trigger. The goal is an
end-to-end visualisation of the request's route in the system,
accompanied by information concerning latencies in each processing
phase. Thanks to LTTng this can happen with a minimal overhead and
in realtime. The LTTng traces can then be visualized with Twitter's
Zipkin_.

.. _Dapper: http://static.googleusercontent.com/media/research.google.com/el//pubs/archive/36356.pdf
.. _Zipkin: http://twitter.github.io/zipkin/

Testing Blkin
=============
It's easy to test Ceph's Blkin tracing. Compile Ceph with the Blkin
changes, then launch Ceph with the vstart script so you can see the
possible tracepoints.::

COMPILE:::
  cd blkin/
  make && make install
  cd ..
  export BLKIN_CFLAGS=-Iblkin/
  export BLKIN_LIBS=-lzipkin-cpp
  ./configure --with-blkin
  make && make install

Restart CEPH DAEMON:::
  cd src
  OSD=3 MON=3 RGW=1 ./vstart.sh -n
  
  /etc/init.d/ceph restart
  
  stop ceph-all
  start ceph-all
  
RUN LTTNG:::
  lttng list --userspace

You'll see something like the following:::

  UST events:
  -------------
  PID: 8987 - Name: ./ceph-osd
        zipkin:timestamp (loglevel: TRACE_WARNING (4)) (type: tracepoint)
        zipkin:keyval (loglevel: TRACE_WARNING (4)) (type: tracepoint)
        ust_baddr_statedump:soinfo (loglevel: TRACE_DEBUG_LINE (13)) (type: tracepoint)

  PID: 8407 - Name: ./ceph-mon
        zipkin:timestamp (loglevel: TRACE_WARNING (4)) (type: tracepoint)
        zipkin:keyval (loglevel: TRACE_WARNING (4)) (type: tracepoint)
        ust_baddr_statedump:soinfo (loglevel: TRACE_DEBUG_LINE (13)) (type: tracepoint)

  ...

Start up an LTTng session and enable the tracepoints.::

  lttng create blkin-test
  lttng enable-event --userspace zipkin:timestamp
  lttng enable-event --userspace zipkin:keyval
  lttng start

You may want to check that ceph is up.::

  ./ceph status

Now put something in usin rados, check that it made it, get it back, and remove it.::

  ./rados mkpool test-blkin
  ./rados put test-object-1 ./vstart.sh --pool=test-blkin
  ./rados -p test-blkin ls
  ./ceph osd map test-blkin test-object-1
  ./rados get test-object-1 ./vstart-copy.sh --pool=test-blkin
  md5sum vstart*
  ./rados rm test-object-1 --pool=test-blkin

You could also use the example in ``examples/librados/``.

Then stop the LTTng session and see what was collected.::

  lttng stop
  lttng view

You'll see something like:::

  [13:09:07.755054973] (+?.?????????) scruffy zipkin:timestamp: { cpu_id = 5 }, { trace_name = "Main", service_name = "MOSDOp", port_no = 0, ip = "0.0.0.0", trace_id = 7492589359882233221, span_id = 2694140257089376129, parent_span_id = 0, event = "Message allocated" }
  [13:09:07.755071569] (+0.000016596) scruffy zipkin:keyval: { cpu_id = 5 }, { trace_name = "Main", service_name = "MOSDOp", port_no = 0, ip = "0.0.0.0", trace_id = 7492589359882233221, span_id = 2694140257089376129, parent_span_id = 0, key = "Type", val = "MOSDOp" }
  [13:09:07.755074217] (+0.000002648) scruffy zipkin:keyval: { cpu_id = 5 }, { trace_name = "Main", service_name = "MOSDOp", port_no = 0, ip = "0.0.0.0", trace_id = 7492589359882233221, span_id = 2694140257089376129, parent_span_id = 0, key = "Reqid", val = "client.4126.0:1" }
  ...

Test Zipkin
===========
Users should run Zipkin as a tracepoints collector and also a web service, 
which means users need to run three services, zipkin-collector, zipkin-query 
and zipkin-web.::

Download Zipkin Package:::
  wget https://github.com/twitter/zipkin/archive/1.1.0.tar.gz
  tar zxf 1.1.0.tar.gz
  cd zipkin-1.1.0
  bin/collector cassandra &
  bin/query cassandra &
  bin/web &

Check Zipkin:::
  bin/test
  browser to http://${zipkin-web}:8080 

Show ceph tracepoint in Zipkin-web
==================================
Blkin also provides a script translates lttng result to zipkin(Dapper) semantics.::

SEND LTTNG DATA TO ZIPKIN:::
  python3 babeltrace_zipkin.py /root/lttng-traces/${blkin-test}/ust/uid/0/64-bit/ -p ${zipkin-collector-port(9410 by default)} -s ${zipkin-collector-ip}
  ex:
  python3 babeltrace_zipkin.py /root/lttng-traces/blkin-test-20150225-160222/ust/uid/0/64-bit/ -p 9410 -s 127.0.0.1

CHECK CEPH TRACEPOINT ON WEBPAGE:::
  Brows http://${zipkin-web-ip}:8080
  Click "Find traces"

