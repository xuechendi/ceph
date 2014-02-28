// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include <errno.h>

#include "common/ceph_context.h"
#include "common/dout.h"
#include "common/Mutex.h"
#include "include/Context.h"
//#include "include/rados/librados.hpp"
//#include "include/rbd/librbd.hpp"

//#include "librbd/AioRequest.h"
#include "librbd/ImageCtx.h"
#include "librbd/internal.h"
#include "KeyValueCacher.h"

#include "include/assert.h"

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "keyValueCacher: "

KeyValueCacher::KeyValueCacher(CephContext *c, const string &path):cct(c),path(path){}

KeyValueCacher::~KeyValueCacher(){
    close();
    db.reset();
}

void KeyValueCacher::open(ostream &out){
  leveldb::DB *_db;
  leveldb::Options ldoptions;
  loptions.create_if_missing = true;
  leveldb::Status status = leveldb::DB::Open(ldoptions, path, &_db);
  db.reset(_db);
  if (!status.ok()) {
    out << status.ToString() << std::endl;
    return -EINVAL;
  }
  return 0;
}


int KeyValueCacher::read(bufferlist &bl, string &kvc_offset){
  std::string value;
  leveldb::Status status = db->Get(leveldb::ReadOptions(), kvc_offset, &value);
  if (!status.ok()) {
    out << status.ToString() << std::endl;
    return -EINVAL;
  }
  bl.append(bufferptr(value.c_str(), value.size()));
  return 0;
}

int KeyValueCacher::write((bufferlist &bl, string &kvc_offset, uint64_t origin_len ){
  keys.push_back(kvc_offset);
  if( origin_len ){
    bat.Delete(leveldb::Slice(*(keys.rbegin())));
  }
  bat.Put(leveldb::Slice(*(keys.rbegin())),leveldb::Slice(bl.c_str(), bl.length()));
  Leveldb::Status status = db->Write(leveldb::WriteOptions(), &bat);
  if (!status.ok()) {
    out << status.ToString() << std::endl;
    return -EINVAL;
  }
  return 0;
}
