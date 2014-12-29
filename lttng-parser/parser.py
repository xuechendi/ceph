import common
import babeltrace
import sys

#============================ main function =============================#

checkpoint_list=['osd:ms_fast_dispatch','pg:queue_op','mutex:lock_PG__lock','osd:opwq_process_start','mutex:lock_OSDService_tid_lock','keyvaluestore:queue_op_finish','osd:opwq_process_finish','keyvaluestore:opwq_process_start','keyvaluestore:opwq_process_finish','keyvaluestore:finish_op','osd:log_op_stats']
#checkpoint_list=['osd:opwq_process_start','osd:opwq_process_finish','filestore:opwq_process_start','filestore:do_transaction_start:op_type=10','filestore:do_transaction_finish:op_type=10','filestore:opwq_process_finish']
#latency_event_list=['filestore:finish_op:latency','osd:log_op_stats:latency','osd:log_op_stats:process_latency']
latency_event_list=['mutex:lock_PG__lock:latency','mutex:lock_OSDService_tid_lock:latency','mutex:lock_OSDService_peer_map_epoch_lock:latency','keyvaluestore:opwq_process_start:get_lock_latency','keyvaluestore:queue_op_finish:queue_len','keyvaluestore:finish_op:latency','mutex:lock_Finisher_finisher_lock:latency','osd:log_op_stats:latency','osd:log_op_stats:process_latency','osd:log_subop_stats:latency']
#latency_event_list=['osd:log_op_stats:latency','osd:log_op_stats:process_latency']

traces = babeltrace.TraceCollection()
lttng_input = "/root/lttng-traces/auto-20141203-091012/ust/uid/0/64-bit/channel10_0/"
if( len(sys.argv)==2 ):
    lttng_input = sys.argv[1]
ret = traces.add_trace( lttng_input, "ctf" )

#========= using checkpoint list to cal the checkpoint interval =======#
chk_inter = common.CheckpointIntervalCal()
chk_inter.parse_trace_to_dict( traces.events, checkpoint_list )
common.print_pid_dict( chk_inter.pid_dict )

#========= using latency_event_list to get latency =======#
#lat_cal=common.LatencyCal()
#lat_cal.parse_trace_to_dict( traces.events, latency_event_list )
#common.print_pid_dict( lat_cal.pid_dict )

#========= using event_name and pthread_id to get thread interval =======#
#thread_inter_cal=common.ThreadInterval()
#thread_inter_cal.parse_trace_to_dict( traces.events, "" )
#common.print_pid_dict( thread_inter_cal.pid_dict )
