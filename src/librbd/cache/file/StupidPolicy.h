// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_LIBRBD_CACHE_FILE_STUPID_POLICY
#define CEPH_LIBRBD_CACHE_FILE_STUPID_POLICY

#include "librbd/cache/file/Policy.h"
#include "include/lru.h"
#include "common/Mutex.h"
#include "librbd/cache/file/Types.h"
#include <unordered_map>
#include <vector>

namespace librbd {

struct ImageCtx;

namespace cache {

struct BlockGuard;

namespace file {

/**
 * Stupid LRU-style policy
 */
template <typename ImageCtxT>
class StupidPolicy : public Policy {
private:

  struct Entry : public LRUObject {
    uint64_t on_disk_id;
    bool in_base_cache;
    Entry() : on_disk_id(0), in_base_cache(false) {
    }
  };

  typedef std::vector<Entry> Entries;
  typedef std::unordered_map<uint64_t, Entry*> BlockToEntries;

  ImageCtxT &m_image_ctx;
  BlockGuard &m_block_guard;

  mutable Mutex m_lock;
  uint64_t m_block_count = 0;

  Entries m_entries;
  BlockToEntries m_block_to_entries;

  LRUList m_free_lru;
  LRUList m_clean_lru;

public:
  StupidPolicy(ImageCtxT &image_ctx, BlockGuard &block_guard);

  virtual void set_block_count(uint64_t block_count);

  virtual int invalidate(uint64_t block);

  virtual int map(IOType io_type, uint64_t block, bool partial_block,
                  PolicyMapResult *policy_map_result,
                  bool in_base_cache = false);
  virtual void tick();
  void set_to_base_cache(uint64_t block);
  uint64_t block_to_offset(uint64_t block) override;
  inline uint64_t get_block_count(){
    return m_block_count;
  }

  virtual uint32_t get_loc(uint64_t block);
  virtual void set_loc(uint32_t *src);

};

} // namespace file
} // namespace cache
} // namespace librbd

extern template class librbd::cache::file::StupidPolicy<librbd::ImageCtx>;

#endif // CEPH_LIBRBD_CACHE_FILE_STUPID_POLICY
