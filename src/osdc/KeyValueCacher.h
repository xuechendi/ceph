// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
#ifndef CEPH_OSDC_KEYVALUECACHER_H
#define CEPH_OSDC_KEYVALUECACHER_H

#include "include/Context.h"
#include "include/types.h"
#include "osd/osd_types.h"
//#include "os/KeyValueDB.h"

#include <boost/scoped_ptr.hpp>
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/write_batch.h"
#include "leveldb/slice.h"
#include "leveldb/cache.h"
#ifdef HAVE_LEVELDB_FILTER_POLICY
#include "leveldb/filter_policy.h"
#endif

#define NOCHANGE 0
#define UPDATE 1
#define DELETE 2

class KeyValueCacher {
  private:
    CephContext *cct;
    string path;
    boost::scoped_ptr<leveldb::DB> db;
    leveldb::WriteBatch bat;
    list<bufferlist> buffers;
    list<string> keys;
    uint64_t stored_data_size;
    map<loff_t, CacheHeader*>& cache_dir;
    class CacheHeader {
    public:
      uint32_t version;
      uint32_t lversion;
      uint64_t length;
      uint64_t key;
    }
  public:
    KeyValueCacher::KeyValueCacher(CephContext *c, const string &path):cct(c),path(path){}
    ~KeyValueCacher() {}
    /*Sync with KeyValueDB*/
    int read(bufferlist *bl, string &kvc_offset);
    int write(bufferlist *bl, string &kvc_offset);
    int do_delete(string &kvc_offset);

    /*LevelDB function*/
    /**
     * find first buffer that includes or follows an offset
     *
     * @param offset object byte offset
     * @return iterator pointing to buffer, or data.end()
     */
    map<loff_t,CacheHeader*>::iterator dir_lower_bound(loff_t offset) {
      map<loff_t,CacheHeader*>::iterator p = cache_dir.lower_bound(offset);
      if (p != cache_dir.begin() &&(p == cache_dir.end() || p->first > offset)) {
	--p;     // might overlap!
      }
      if (p->first + p->second->length() <= offset){
	++p;   // doesn't overlap.go back to last p
      }
      return p;
    }

    int cache_dir_op(CacheHeader *ch){
      int op = NOCHANGE;
      if(ch->version == 0)
	op = DELETE;
      else if(ch->version > ch->lversion)
	op = UPDATE;
      return op;
    }

    int open(ostream &out);
    uint64_t get_size(){return stored_data_size}
    void set_size(int32_t change){ stored_data_size += change;}

};
#endif

