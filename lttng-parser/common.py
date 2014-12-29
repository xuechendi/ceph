import sys
import re
import numpy
from datetime import datetime

def cal_avg_without_zero( data_list ):
    res_sum=0
    count=0
    for value in data_list:
        if( value != 0):
            res_sum+=value
            count+=1
    if( count == 0 ):
        res = 0
    else:
        res=res_sum/count
    return res

def print_pid_dict( pid_output_dict ):
    for pid in pid_output_dict:
        print("========="+str(pid)+"============")
        output_dict=pid_output_dict[pid]['data_dict']
        for key in output_dict:
            output=""
            for data in output_dict[key]:
                output += ','+str(data)
            avg_output=numpy.mean(output_dict[key])
            #avg_output=cal_avg_without_zero(output_dict[key])
            #print(key+","+str(avg_output))
            print(key+","+str(avg_output)+output)
            #print(key+","+output)

class Calculator:
    pid_dict={} 
    typename_dict={}
    typename_count=0
    first_event=""    
    last_event=""
    reqid_dict={}

    def parse_typename_list(self, typename_list):
        if( typename_list == "" ):
            return
        self.first_event=re.match(r'(\w+:\w+):*(\w*)', typename_list[0] ).group(1)
        self.last_event=re.match(r'(\w+:\w+):*(\w*)', typename_list[-1] ).group(1)
        for key in typename_list:
            self.typename_count+=1
            m = re.match(r'(\w+:\w+):*(\w*=*\w*)', key )
            if(m.group(1) not in self.typename_dict):
                self.typename_dict[m.group(1)]={key:m.group(2)}
            else:
                self.typename_dict[m.group(1)][key]=m.group(2)

    def parse_trace_to_dict( self, trace_dict, typename_list ):
        self.parse_typename_list( typename_list )
        for event in trace_dict:
            pid=event['vpid']
            self.pid_dict_update( pid, typename_list )
            
            data_dict=self.pid_dict[pid]['data_dict']
            one_round_record=self.pid_dict[pid]['one_round_record']
            typename_dict=self.typename_dict       
            event_name=self.get_eventname(event)
     
            if( event_name not in self.typename_dict ):
                continue
            
            #record one round checkpoint into dict first
            if( self.try_one_round_record_update( event, one_round_record ) ):
                continue
            else:
                self.update_one_round_to_data_dict( typename_list, one_round_record, data_dict, event )    
                self.force_one_round_record_update( event, one_round_record )
        return self.pid_dict

    def parse_param( self, param ):
        m = re.match(r'(\w+)=(\w+)', param )
        if(m.group(1) and m.group(2)):
            return {'key':m.group(1),'value':int(m.group(2))}
        return {'key':"",'value':0}

    def get_reqid( self, event ):
        if( 'num' in event and 'tid' in event ):
            reqid=str(event['num'])+str(event['tid'])
            self.reqid_dict[event['pthread_id']]=reqid
        else:
            if( event['pthread_id'] in self.reqid_dict ):
                reqid=self.reqid_dict[event['pthread_id']]
            else:
                reqid=""
        return reqid

    def del_from_reqid( self, reqid ):
        rm_list=[]
        for key,value in self.reqid_dict.items():
            if( reqid == value ):
                rm_list.append(key)
        for key in rm_list:        
            del self.reqid_dict[ key ]

    def get_eventname(self, event):
        try:
            extra=re.sub(':{2}','_',event['extra_eventname']);
            event_name=event.name+"_"+extra
        except:
            event_name=event.name
        return event_name

class CheckpointIntervalCal(Calculator):
    def pid_dict_update( self, pid, typename_list ):
        if(pid not in self.pid_dict):
            self.pid_dict[pid]={'data_dict':{},'one_round_record':{}}
            self.pid_dict[pid]['data_dict']={}
            self.pid_dict[pid]['one_round_record']={}
            for eventname in typename_list:
                if( eventname == typename_list[0] ):
                    lastname = eventname
                else:
                    self.pid_dict[pid]['data_dict'][lastname+"-"+eventname]=[]
                    lastname = eventname

    def try_one_round_record_update( self, event, one_round_record ):
        reqid=self.get_reqid(event)
        if(reqid == ""):
            reqid = event['pthread_id']
        elif( event['pthread_id'] in one_round_record ):
            tmp_events = one_round_record[event['pthread_id']].keys()
            for tmp_event in tmp_events:
                print(tmp_event+"  "+str(event['pthread_id'])+"  "+str(reqid)+"  "+str(one_round_record[event['pthread_id']][tmp_event]))
                one_round_record[reqid][tmp_event] = one_round_record[event['pthread_id']][tmp_event]
            del one_round_record[event['pthread_id']]
        event_name=self.get_eventname(event)
        if( reqid not in one_round_record ):
            one_round_record[reqid]={}
        for key,value in self.typename_dict[event_name].items():
            if( value == "" ):
                one_round_record[reqid][key]=event.timestamp
            elif( value != "" ):
                param_dict=self.parse_param(value)
                if(event[param_dict['key']] != param_dict['value']):
                    continue
                if( key not in one_round_record[reqid] ):
                    one_round_record[reqid][key]={event[param_dict['key']]:event.timestamp}
                else:
                    one_round_record[reqid][key][event[param_dict['key']]]=event.timestamp
        if( event_name == self.last_event ):
            self.del_from_reqid( reqid )
            return False
        else:
            return True

    def force_one_round_record_update( self, event, one_round_record ):
        pass

    def update_one_round_to_data_dict( self, typename_list, one_round_record, data_dict, event ):
        reqid=self.get_reqid(event)
        #reqid=str(event['num'])+str(event['tid'])
        last_time = 0
        last_typename = ""
        for key in typename_list:
            if( key not in one_round_record[reqid] ):
                continue
            if(key == typename_list[0] or last_typename == "" ):
                last_time=one_round_record[reqid][key]
                last_typename=key
                continue
            dict_key = last_typename+"-"+key
            #if( dict_key not in data_dict ):
                #data_dict[dict_key]=[]
            if(type(one_round_record[reqid][key]) is int):
                if( dict_key in data_dict ):
                    print(key+"  "+str(one_round_record[reqid][key]))
                    data_dict[dict_key].append(one_round_record[reqid][key]-last_time)
                last_time=one_round_record[reqid][key]
            else:
                value=re.match(r'(\w+:\w+):*(\w*\W*\w*)', key ).group(2)
                param_dict=self.parse_param(value)
                if( dict_key in data_dict ):
                    data_dict[dict_key].append(one_round_record[reqid][key][param_dict['value']]-last_time)
                last_time=one_round_record[reqid][key][param_dict['value']]
            last_typename=key
        del one_round_record[reqid]

class LatencyCal(Calculator):
    def parse_trace_to_dict( self, trace_dict, typename_list ):
        self.parse_typename_list( typename_list )
        for event in trace_dict:
            pid=event['vpid']
            self.pid_dict_update( pid, typename_list  )
            
            data_dict=self.pid_dict[pid]['data_dict']
            typename_dict=self.typename_dict       
            event_name=self.get_eventname(event)
     
            if( event_name not in self.typename_dict and typename_list != ""):
                continue
            
            #record one round checkpoint into dict first
            for key,value in self.typename_dict[event_name].items():
                data_dict[key].append( event[value] )
        return self.pid_dict
    
    def pid_dict_update( self, pid, typename_list ):
        if(pid not in self.pid_dict):
            self.pid_dict[pid]={'data_dict':{},'one_round_record':{}}
            for key in typename_list:
                self.pid_dict[pid]['data_dict'][key]=[]

class ThreadInterval(Calculator):
    def parse_trace_to_dict( self, trace_dict, typename_list ):
        self.parse_typename_list( typename_list )
        for event in trace_dict:
            pid=event['vpid']
            self.pid_dict_update( pid )
            
            data_dict=self.pid_dict[pid]['data_dict']
            one_round_record=self.pid_dict[pid]['one_round_record']
            typename_dict=self.typename_dict       
            event_name=self.get_eventname(event)
     
            if( event_name not in self.typename_dict and typename_list != ""):
                continue
            
            #record one round checkpoint into dict first
            if( self.try_one_round_record_update( event, one_round_record ) ):
                continue
            else:
                self.update_one_round_to_data_dict( typename_list, one_round_record, data_dict, event )    
                self.force_one_round_record_update( event, one_round_record )
        return self.pid_dict

    def pid_dict_update( self, pid ):
        if(pid not in self.pid_dict):
            self.pid_dict[pid]={'data_dict':{},'one_round_record':{}}
    
    def try_one_round_record_update( self, event, one_round_record ):
        event_name=self.get_eventname(event)
        key=event_name+"-"+str(event['pthread_id'])
        if( key not in one_round_record ):
            one_round_record[key] = event.timestamp 
            return True
        else:
            return False
        
    def update_one_round_to_data_dict( self, typename_list, one_round_record, data_dict, event ):
        event_name=self.get_eventname(event)
        key=event_name+"-"+str(event['pthread_id'])
        if( key not in data_dict ):
            data_dict[key]=[event.timestamp-one_round_record[key]]
        else:
            data_dict[key].append(event.timestamp-one_round_record[key])

    def force_one_round_record_update( self, event, one_round_record ):
        event_name=self.get_eventname(event)
        key=event_name+"-"+str(event['pthread_id'])
        one_round_record[key] = event.timestamp 
        
#Description:
#output as a dict, each pid will contain a checkpoint interval dict to list checkpoint intervals
#notice, if one checkpoint is missing, the interval before which and after will be 0
#notice, each row is one round of all checkpoint
#todo, when cal the avg, need to skip all zero   

#============================ backup =============================#

#lockers = {}
#for event in traces.events:
#    # metadata about mutex and thread context
#    addr, name = event["addr"], event["name"]
#    thread_id = event["pthread_id"]
#
#    # identifies a thread interacting with a mutex
#    locker = (thread_id, addr)
#
#    # track when we finally acquire the lock
#    if event.name == "mutex:lock_exit":
#        lockers[locker] = event.timestamp
#
#    # calc time from Mutex::Lock to Mutex::Unlock
#    elif event.name == "mutex:unlock":
#        try:
#            duration = event.timestamp - lockers[locker]
#            del lockers[locker]
#            print duration, name
#        except KeyError:
#            continue
