// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_LIBRBD_CACHE_FILE_META_STORE
#define CEPH_LIBRBD_CACHE_FILE_META_STORE

#include "librbd/cache/Types.h"
#include "include/int_types.h"
#include "librbd/cache/file/AioFile.h"
#include "common/Mutex.h"

struct Context;

namespace librbd {

struct ImageCtx;

namespace cache {
namespace file {

template <typename ImageCtxT>
class MetaStore {
public:
  MetaStore(ImageCtxT &image_ctx, uint32_t block_size);

  void init(bufferlist *bl, Context *on_finish);
  void set_entry_size(uint32_t entry_size);
  void reset(Context *on_finish);
  void write_block(uint64_t cache_block, bufferlist bl, Context *on_finish);
  void read_block(uint64_t cache_block, bufferlist *bl, Context *on_finish);
  void load_all(bufferlist* bl, Context *on_finish);
  void shut_down(Context *on_finish);

  inline uint64_t offset_to_block(uint64_t offset) {
    return offset / m_block_size;
  }

  inline uint64_t block_to_offset(uint64_t block) {
    return block * m_block_size;
  }

private:
  ImageCtxT &m_image_ctx;
  uint32_t m_block_size;
  uint32_t m_entry_size;

  AioFile<ImageCtx> m_meta_file;

};

} // namespace file
} // namespace cache
} // namespace librbd

extern template class librbd::cache::file::MetaStore<librbd::ImageCtx>;

#endif // CEPH_LIBRBD_CACHE_FILE_META_STORE
