// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "librbd/cache/file/SimpleLogPolicy.h"
#include "common/dout.h"
#include "librbd/ImageCtx.h"
#include "librbd/cache/BlockGuard.h"

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::cache::file::SimpleLogPolicy: " << this \
                           << " " <<  __func__ << ": "

#define BLOCK_SIZE 4096

namespace librbd {
namespace cache {
namespace file {

template <typename I>
SimpleLogPolicy<I>::SimpleLogPolicy(I &image_ctx, BlockGuard &block_guard)
  : m_image_ctx(image_ctx), m_block_guard(block_guard),
    m_lock("librbd::cache::file::SimpleLogPolicy::m_lock") {

  // TODO support resizing of entries based on number of provisioned blocks
  //om_entries.resize(m_image_ctx.ssd_cache_size / BLOCK_SIZE); // 1GB of storage
  for( uint64_t index = 0; index < (m_image_ctx.ssd_cache_size / BLOCK_SIZE); i++) {
    m_entries.push_back(Entry(index * BLOCK_SIZE));
  }
  for (auto &entry : m_entries) {
    m_free_lru.insert_tail(&entry);
  }
}

template <typename I>
void SimpleLogPolicy<I>::set_write_mode(uint8_t write_mode) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "write_mode=" << write_mode << dendl;

  // TODO change mode on-the-fly
  Mutex::Locker locker(m_lock);
  m_write_mode = write_mode;

}

template <typename I>
uint8_t SimpleLogPolicy<I>::get_write_mode() {
  CephContext *cct = m_image_ctx.cct;

  // TODO change mode on-the-fly
  Mutex::Locker locker(m_lock);
  return m_write_mode;

}

template <typename I>
void SimpleLogPolicy<I>::set_block_count(uint64_t block_count) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "block_count=" << block_count << dendl;

  // TODO ensure all entries are in-bound
  Mutex::Locker locker(m_lock);
  m_block_count = block_count;

}

template <typename I>
int SimpleLogPolicy<I>::invalidate(uint64_t block) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "block=" << block << dendl;

  // TODO handle case where block is in prison (shouldn't be possible
  // if core properly registered blocks)

  Mutex::Locker locker(m_lock);
  auto entry_it = m_block_to_entries.find(block);
  if (entry_it == m_block_to_entries.end()) {
    return 0;
  }

  Entry *entry = entry_it->second;
  m_block_to_entries.erase(entry_it);

  LRUList *lru;
  if (entry->dirty) {
    lru = &m_dirty_lru;
  } else {
    lru = &m_clean_lru;
  }
  lru->remove(entry);

  m_free_lru.insert_tail(entry);
  return 0;
}

template <typename I>
bool SimpleLogPolicy<I>::contains_dirty() const {
  Mutex::Locker locker(m_lock);
  return m_dirty_lru.get_tail() != nullptr;
}

template <typename I>
bool SimpleLogPolicy<I>::is_dirty(uint64_t block) const {
  Mutex::Locker locker(m_lock);
  auto entry_it = m_block_to_entries.find(block);
  assert(entry_it != m_block_to_entries.end());

  bool dirty = entry_it->second->dirty;

  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "block=" << block << ", "
                 << "dirty=" << dirty << dendl;
  return dirty;
}

template <typename I>
void SimpleLogPolicy<I>::update_meta_info(MetaInfo meta_info) {
  if (meta_info.dirty == true){
    set_dirty(meta_info.block_id);
  }else{
    set_clean(meta_info.block_id);
  }
  touch(meta_info.block_id);
}

template <typename I>
void SimpleLogPolicy<I>::set_dirty(uint64_t block) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "block=" << block << dendl;

  Mutex::Locker locker(m_lock);
  auto entry_it = m_block_to_entries.find(block);
  assert(entry_it != m_block_to_entries.end());

  Entry *entry = entry_it->second;
  if (entry->replace_entry!=nullptr) {
    Entry* new_entry = replace_entry;
    m_block_to_entries[block] = new_entry;

    /*return old entry to free_list*/
    m_clean_lru.remove(entry);
    m_dirty_lru.remove(entry);
    entry->reset();
    m_free_lru.insert_head(entry);

    entry = new_entry;
  }

  entry->dirty = true;
  entry->ts = time();
}

template <typename I>
void SimpleLogPolicy<I>::clear_dirty(uint64_t block) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "block=" << block << dendl;

  Mutex::Locker locker(m_lock);
  auto entry_it = m_block_to_entries.find(block);
  assert(entry_it != m_block_to_entries.end());

  Entry *entry = entry_it->second;
  if (!entry->dirty) {
    return;
  }

  entry->dirty = false;
  entry->ts = time();
  m_dirty_lru.remove(entry);
  m_clean_lru.insert_head(entry);
}

template <typename I>
int SimpleLogPolicy<I>::get_writeback_blocks(std::map<uint64_t, uint64_t> &&block_map) {
  CephContext *cct = m_image_ctx.cct;

  Mutex::Locker locker(m_lock);
  //1. dirty ratio exceeds throttle.
  //2. dirty block updated 10sec ago.
  Entry *entry;
  uint64_t dirty_entry_count = get_dirty_entry_length_by_ratio(m_max_dirty_ratio);
  time_t cur = time();
  for (uint64_t i = 0; i < dirty_entry_count || cur - entry->ts >= 10; i++) {
    entry = reinterpret_cast<Entry*>(m_dirty_lru.get_tail());
    if (entry == nullptr) {
      ldout(cct, 20) << "no dirty blocks to writeback" << dendl;
      break;
    }
    assert(entry->dirty);
    block_map[entry->block_io->block] = entry->block_id;
  }
  if (block_map.length() == 0) {
    ldout(cct, 20) << "no dirty blocks to writeback" << dendl;
    return -ENODATA;
  }
  return 0;
}

template <typename I>
int SimpleLogPolicy<I>::map(IOType io_type, BlockGuard::BlockIO* block_io,
                         PolicyMapResult *policy_map_result,
                         uint64_t *on_disk_offset) {
  CephContext *cct = m_image_ctx.cct;
  uint64_t block = block_io->block;
  ldout(cct, 20) << "block=" << block << dendl;

  Mutex::Locker locker(m_lock);
  Entry *entry;
  auto entry_it = m_block_to_entries.find(block);
  if (entry_it != m_block_to_entries.end()) {
    // cache hit -- move entry to the front of the queue
    // but we do log append rather then write in place
    ldout(cct, 20) << "cache hit" << dendl;
    *policy_map_result = POLICY_MAP_RESULT_HIT;

    if (io_type == IO_TYPE_WRITE) {
      //for write, we will assign a new entry
      entry = reinterpret_cast<Entry*>(m_free_lru.get_head());
      if (entry != nullptr) {
          entry_it->second.replace_entry = entry;
      }
    }else{
      entry = entry_it->second;
    }
    on_disk_offset = entry.on_disk_off;
    return 0;
  }

  // cache miss
  entry = reinterpret_cast<Entry*>(m_free_lru.get_head());
  if (entry != nullptr) {
    // entries are available -- allocate a slot
    ldout(cct, 20) << "cache miss -- new entry" << dendl;
    *policy_map_result = POLICY_MAP_RESULT_NEW;
    m_free_lru.remove(entry);

    entry->block_io = block_io;
    m_block_to_entries[block] = entry;
    on_disk_offset = entry.on_disk_off;
    return 0;
  }

  // if we have clean entries we can demote, attempt to steal the oldest
  /*entry = reinterpret_cast<Entry*>(m_clean_lru.get_tail());
  if (entry != nullptr) {
    int r = m_block_guard.detain(entry->block, nullptr);
    if (r >= 0) {
      ldout(cct, 20) << "cache miss -- replace entry" << dendl;
      *policy_map_result = POLICY_MAP_RESULT_REPLACE;
      *replace_cache_block = entry->block;

      m_block_to_entries.erase(entry->block);
      m_clean_lru.remove(entry);

      entry->block = block;
      m_block_to_entries[block] = entry;
      m_clean_lru.insert_head(entry);
      return 0;
    }
    ldout(cct, 20) << "cache miss -- replacement deferred" << dendl;
  } else {
    ldout(cct, 20) << "cache miss" << dendl;
  }*/

  // no clean entries to evict -- treat this as a miss
  *policy_map_result = POLICY_MAP_RESULT_MISS;
  return 0;
}

template <typename I>
void SimpleLogPolicy<I>::touch(uint64_t block) {
  Mutex::Locker locker(m_lock);
  auto entry_it = m_block_to_entries.find(block);
  assert(entry_it != m_block_to_entries.end());

  Entry *entry = entry_it->second;
  LRUList *lru;
  if (entry->dirty) {
    lru = &m_dirty_lru;
  } else {
    lru = &m_clean_lru;
  }

  lru->remove(entry);
  lru->insert_head(entry);
}

template <typename I>
int SimpleLogPolicy<I>::get_entry_size() {
  return sizeof(uint64_t);
}

template <typename I>
void SimpleLogPolicy<I>::entry_to_bufferlist(uint64_t block, bufferlist *bl){
  Mutex::Locker locker(m_lock);
  auto entry_it = m_block_to_entries.find(block);
  assert(entry_it != m_block_to_entries.end());

  //TODO
  Entry_t entry;
  entry.block = entry_it->second->block_io->block;
  entry.dirty = entry_it->second->dirty;
  entry.on_disk_off = entry_it->second->on_disk_off;
  bufferlist encode_bl;
  entry.encode(encode_bl);
  bl->append(encode_bl);
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 1) << "block=" << block << " encode_bl=" << encode_bl << dendl;
}

template <typename I>
void SimpleLogPolicy<I>::bufferlist_to_entry(bufferlist &bl){
  Mutex::Locker locker(m_lock);
  uint64_t entry_index = 0;
  //TODO
  Entry_t entry;
  for (bufferlist::iterator it = bl.begin(); it != bl.end(); ++it) {
	entry.decode(it);
	auto entry_it = m_entries[entry_index++];
	//TODO: create BlockIO
	//entry_it.block = entry.block;
	entry_it.dirty = entry.dirty;
  }
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "Total load " << entry_index << " entries" << dendl;

}
  
template <typename I>
virtual inline uint64_t  SimpleLogPolicy<I>::offset_to_block(uint64_t offset) {
  return offset / m_block_size;
}

template <typename I>
virtual inline uint64_t  SimpleLogPolicy<I>::block_to_offset(uint64_t block) {
  return offset / m_block_size;
}


} // namespace file
} // namespace cache
} // namespace librbd

template class librbd::cache::file::SimpleLogPolicy<librbd::ImageCtx>;
