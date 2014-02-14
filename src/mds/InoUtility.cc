
#include "mds/InoUtility.h"
#include "mds/CInode.h"
#include "common/Mutex.h"
#include "mds/inode_backtrace.h"
#include "include/encoding.h"

#include "include/rados/librados.hpp"
using namespace librados;

#define dout_subsys ceph_subsys_mds


void InoUtility::by_path(std::string const &path)
{

}


__u32 hash_dentry_name(inode_t const & inode, const string &dn)
{
  int which = inode.dir_layout.dl_dir_hash;
  if (!which)
    which = CEPH_STR_HASH_LINUX;
  return ceph_str_hash(which, dn.data(), dn.length());
}


void InoUtility::by_id(inodeno_t const id)
{
  dout(4) << __func__ << dendl;
  // Go get the backtrace object, then follow

  // Resolve inode ID to object ID
  // TODO: cope with fragmented directory, have to get multiple
  // objects;
  // TODO: what's suffix to get_object_name and when would I need it?
  object_t oid = CInode::get_object_name(id, frag_t(), "");
  object_locator_t oloc(mdsmap->get_metadata_pool());

  // RADOS xattr read to obtain backtrace
  bufferlist parent_bl;
//  ObjectOperation rd_parent;

  // FIXME: this is a faff, can I use synchronous librados ops or something?
//  int r;
//  bool done = false;
//  Cond cond;
//
//  // We need a separate lock for use with SafeCond, because it
//  // takes the lock in completion, and we already hold .lock
//  // while dispatching messages causing completion.
//  Mutex localLock("dump:lock");
//  C_SafeCond *c = new C_SafeCond(&localLock, &cond, &done, &r);
//
//  rd_parent.getxattr("parent", &parent_bl, NULL);
//
//  dout(4) << "Sending xattr read for object " << oid << dendl;

  // Take the lock while doing remote ops.
//  lock.Lock();
//  objecter->read(oid, oloc, rd_parent, CEPH_NOSNAP, NULL, 0, c);
//  lock.Unlock();
//
//  // Wait for xattr read
//  localLock.Lock();
//  while (!done) {
//    cond.Wait(localLock);
//  }
//  localLock.Unlock();
//  if (r < 0) {
//    dout(0) << "Error getting xattr: " << r << dendl;
//    return;
//  }

  Rados rados;
  rados.init("admin");
  // FIXME global conf init handles this for objecter, where shall I get it from?
  rados.conf_read_file("./ceph.conf");
  rados.connect();
  IoCtx ioctx;
  // FIXME this takes pool by name, we have ID
  rados.ioctx_create("metadata", ioctx);
  int r = ioctx.getxattr(oid.name, "parent", parent_bl);
  if (r < 0) {
    dout(0) << "Error getting xattr: " << r << dendl;
    rados.shutdown();
    return;
  }

  inode_backtrace_t backtrace;
  ::decode(backtrace, parent_bl);

  // The backtrace looks a bit like this:
  //{"ino":1099511635765,"ancestors":[
  //  {"dirino":1099511635187,"dname":"577","version":1156},
  //  {"dirino":1,"dname":"3","version":52181}],"pool":1,"old_pools":[]
  //}

  JSONFormatter jf(true);
  jf.open_object_section("backtrace");
  backtrace.dump(&jf);
  jf.close_section();
  jf.flush(std::cout);
  std::cout << std::endl;

  // Print the path we're going to traverse, it'll make debugging subsequent
  // barfs easier.
  std::string path = "/";
  std::vector<inode_backpointer_t>::reverse_iterator i;
  for (i = backtrace.ancestors.rbegin(); i != backtrace.ancestors.rend(); ++i) {
      // Resolve dirino+dname into a fragment ID
      path += i->dname + "/";
  }
  std::cout << "Path: " << path << std::endl;

  // First, bootstrap the parent_inode and parent_fragtree from MDS_INO_ROOT
  // Go get the root inode .inode object, it will tell me about
  // the root inode's directory fragmentation
  // The .inode object comes from CInode::store, which writes the magic
  // and then uses encode_store
  // I don't really want to use the C* classes, but in this instance it's where
  // the decode logic lives
  object_t root_oid = CInode::get_object_name(MDS_INO_ROOT, frag_t(), ".inode");
  bufferlist root_inode_bl;
  int whole_file = 1 << 22;  // FIXME: magic number, this is what rados.cc uses for default op_size
  ioctx.read(root_oid.name, root_inode_bl, whole_file, 0);
  string magic;
  bufferlist::iterator root_inode_bl_iter = root_inode_bl.begin();
  ::decode(magic, root_inode_bl_iter);
  std::cout << "Magic: " << magic << std::endl;
  if (magic != std::string(CEPH_FS_ONDISK_MAGIC)) {
    dout(0) << "Bad magic '" << magic << "' in root inode" << dendl;
    assert(0);
  }

  fragtree_t parent_fragtree;
  inode_t parent_ino;

  string symlink;
  // This is the first part of CInode::decode_store, which we can't access
  // without constructing the mdcache that a cinode instance would want.
  DECODE_START_LEGACY_COMPAT_LEN(4, 4, 4, root_inode_bl_iter);
  ::decode(parent_ino, root_inode_bl_iter);
  if (parent_ino.is_symlink()) {
    ::decode(symlink, root_inode_bl_iter);
  }
  ::decode(parent_fragtree, root_inode_bl_iter);
  DECODE_FINISH(root_inode_bl_iter);

  for (i = backtrace.ancestors.rbegin(); i != backtrace.ancestors.rend(); ++i) {
    // Identify the fragment in the parent which will hold the next dentry+inode
    // Hash a la CInode::hash_dentry_name
    frag_t frag = parent_fragtree[hash_dentry_name(parent_ino, i->dname)];
    object_t frag_object_id = CInode::get_object_name(parent_ino.ino, frag, "");
    dout(4) << "frag_object_id: " << frag_object_id << dendl;

    // Retrieve the next dentry from the parent directory fragment
    bufferlist inode_object_data;
    std::map<std::string, bufferlist> omap_out;
    // FIXME: support snaps other than head
    std::string dentry_name = i->dname + std::string("_head");
    // FIXME: relying on prefix of dentry name to be unique, can this be
    // broken by snaps with underscores in name perhaps like 123_head vs 123_headusersnap?  or
    // is the snap postfix an ID?
    ioctx.omap_get_vals(
	frag_object_id.name,
	"", dentry_name, 11, &omap_out);
    if (omap_out.size() != 1) {
      // TODO: report nicely when there is an inconsistency like this
      assert(0);
    }
    if (omap_out.find(dentry_name) == omap_out.end()) {
      // TODO: report nicely when there is an inconsistency like this
      dout(0) << "Missing dentry '" << dentry_name << "' in object '" << frag_object_id.name << "'" << dendl;
      assert(0);
    } else {
      dout(4) << "Read dentry " << omap_out[dentry_name].length() << " bytes" << dendl;
    }
    std::cout << "Read dentry '" << dentry_name << "' from fragment '" << frag_object_id << "' of parent '" << parent_ino.ino << "'" << std::endl;

    // Decode the dentry, as encoded by CDir::_encode_dentry
    bufferlist &next_dentry_bl = omap_out[dentry_name];
    string next_symlink;
    char type;
    bufferlist::iterator next_dentry_bl_iter = next_dentry_bl.begin();
    snapid_t dentry_snapid;
    ::decode(dentry_snapid, next_dentry_bl_iter);
    ::decode(type, next_dentry_bl_iter);
    if (type != 'I') {
      dout(0) << "Unknown type '" << type << "'" << dendl;
      assert(type == 'I'); // TODO: handle is_remote branch
    }
    ::decode(parent_ino, next_dentry_bl_iter);
    dout(10) << "next_ino.ino: " << parent_ino.ino << dendl;
    if (parent_ino.is_symlink()) {
      ::decode(next_symlink, next_dentry_bl_iter);
    }
    ::decode(parent_fragtree, next_dentry_bl_iter);
  }

//
//  // Decode the inode object
//  inode_t ino;
//  ino.decode(bl);
//
//  // Print the object
//  JSONFormatter jf;
//  jf.open_object_section("inode");
//  ino.dump(&jf);
//  jf.close_section();
//  jf.flush(std::cout);
}


//void InoUtility::follow_ino(
//    parent_
//    )
