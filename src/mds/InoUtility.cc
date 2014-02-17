
#include "common/Mutex.h"
#include "mds/inode_backtrace.h"
#include "include/encoding.h"
#include "mds/CInode.h"

#include <boost/algorithm/string.hpp>

#include "mds/InoUtility.h"


#define dout_subsys ceph_subsys_mds


// FIXME: refactor main declaration of this so we don't have
// to duplicate it here.
__u32 hash_dentry_name(inode_t const & inode, const string &dn)
{
  int which = inode.dir_layout.dl_dir_hash;
  if (!which)
    which = CEPH_STR_HASH_LINUX;
  return ceph_str_hash(which, dn.data(), dn.length());
}


int InoUtility::init()
{
  int rc = MDSUtility::init();

  rados.init("admin");
  // FIXME global conf init handles this for objecter, where shall I get it from?
  rados.conf_read_file("./ceph.conf");
  rados.connect();

  rados.ioctx_create("metadata", md_ioctx);  // FIXME this takes pool by name, we have ID
  rados.ioctx_create("data", data_ioctx);  // FIXME this takes pool by name, we have ID

  // Populate root_ino and root_fragtree
  object_t root_oid = CInode::get_object_name(MDS_INO_ROOT, frag_t(), ".inode");
  bufferlist root_inode_bl;
  int whole_file = 1 << 22;  // FIXME: magic number, this is what rados.cc uses for default op_size
  md_ioctx.read(root_oid.name, root_inode_bl, whole_file, 0);
  string magic;
  string symlink;
  bufferlist::iterator root_inode_bl_iter = root_inode_bl.begin();
  ::decode(magic, root_inode_bl_iter);
  std::cout << "Magic: " << magic << std::endl;
  if (magic != std::string(CEPH_FS_ONDISK_MAGIC)) {
    dout(0) << "Bad magic '" << magic << "' in root inode" << dendl;
    assert(0);
  }
  // This is the first part of CInode::decode_store, which we can't access
  // without constructing the mdcache that a cinode instance would want.
  DECODE_START_LEGACY_COMPAT_LEN(4, 4, 4, root_inode_bl_iter);
  ::decode(root_ino, root_inode_bl_iter);
  if (root_ino.is_symlink()) {
    ::decode(symlink, root_inode_bl_iter);
  }
  ::decode(root_fragtree, root_inode_bl_iter);
  DECODE_FINISH(root_inode_bl_iter);

  return rc;
}

void InoUtility::shutdown()
{
  MDSUtility::shutdown();

  rados.shutdown();
}


void InoUtility::by_path(std::string const &path)
{
  std::vector<std::string> dnames;
  boost::split(dnames, path, boost::is_any_of("/"));

  // Inode properties read through the path
  fragtree_t parent_fragtree = root_fragtree;
  inode_t parent_ino = root_ino;

  for (std::vector<std::string>::iterator dn = dnames.begin(); dn != dnames.end(); ++dn) {
    if (dn->empty()) {
      // FIXME get a better split function
      continue;
    }
    // Identify the fragment in the parent which will hold the next dentry+inode
    // Hash a la CInode::hash_dentry_name
    frag_t frag = parent_fragtree[hash_dentry_name(parent_ino, *dn)];
    object_t frag_object_id = CInode::get_object_name(parent_ino.ino, frag, "");
    dout(4) << "frag_object_id: " << frag_object_id << dendl;

    // Retrieve the next dentry from the parent directory fragment
    bufferlist inode_object_data;
    std::map<std::string, bufferlist> omap_out;
    // FIXME: support snaps other than head
    std::string dentry_name = *dn + std::string("_head");
    // FIXME: relying on prefix of dentry name to be unique, can this be
    // broken by snaps with underscores in name perhaps like 123_head vs 123_headusersnap?  or
    // is the snap postfix an ID?
    md_ioctx.omap_get_vals(
      frag_object_id.name,
      "", dentry_name, 11, &omap_out);
    if (omap_out.size() != 1) {
      // TODO: report nicely when there is an inconsistency like this
      dout(0) << "Missing dentry '" << dentry_name << "' in object '" << frag_object_id.name << "'" << dendl;
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
      string symlink;
      ::decode(symlink, next_dentry_bl_iter);
    }
    ::decode(parent_fragtree, next_dentry_bl_iter);

    // Print the inode
    JSONFormatter jf(true);
    jf.open_object_section("inode");
    parent_ino.dump(&jf);
    jf.close_section();
    jf.flush(std::cout);
    std::cout << std::endl;
  }
  std::cout << "Path '" << path << "' is inode " << std::hex << parent_ino.ino << std::dec << std::endl;
}


/**
 * Use backtrace data to locate an inode by ID
 *
 * You'd think you could just read the inode_backpointer_t and resolve
 * the inode, but you'd be wrong: to load the inode we need to know
 * the fragtree of the parent, and for which we have to load the parent's
 * inode, and by induction all the ancestor inodes up to the root.  So
 * this method does a full traversal and validates that the dentries
 * match up with the backtrace in the process.
 */
void InoUtility::by_id(inodeno_t const id)
{
  dout(4) << __func__ << dendl;

  // Read backtrace object.  Because we don't know ahead of time
  // whether this is a directory, we must look in both the default
  // data pool (non-directories) and the metadata pool (directories)
  bufferlist parent_bl;
  object_t oid = CInode::get_object_name(id, frag_t(), "");
  int rc = md_ioctx.getxattr(oid.name, "parent", parent_bl);
  if (rc) {
    dout(4) << "Backtrace for '" << id << "' not found in metadata pool" << dendl;
    int rc = data_ioctx.getxattr(oid.name, "parent", parent_bl);
    if (rc) {
      dout(0) << "No backtrace found for inode '" << id << "'" << dendl;
      return;
    }
  }

  // We got the backtrace data, decode it.
  inode_backtrace_t backtrace;
  ::decode(backtrace, parent_bl);
  JSONFormatter jf(true);
  jf.open_object_section("backtrace");
  backtrace.dump(&jf);
  jf.close_section();
  jf.flush(std::cout);
  std::cout << std::endl;

  // Print the path we're going to traverse
  std::string path = "/";
  std::vector<inode_backpointer_t>::reverse_iterator i;
  for (i = backtrace.ancestors.rbegin(); i != backtrace.ancestors.rend(); ++i) {
      // Resolve dirino+dname into a fragment ID
      path += i->dname + "/";
  }
  std::cout << "Backtrace path for inode '" << id << "' : '" << path << "'" << std::endl;

  // Inode properties read through the backtrace
  fragtree_t parent_fragtree = root_fragtree;
  inode_t parent_ino = root_ino;

  for (i = backtrace.ancestors.rbegin(); i != backtrace.ancestors.rend(); ++i) {
    // TODO: validate that the backtrace's dirino value matches up
    // with the ino that we are finding by resolving dentrys by name

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
    md_ioctx.omap_get_vals(
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
      string symlink;
      ::decode(symlink, next_dentry_bl_iter);
    }
    ::decode(parent_fragtree, next_dentry_bl_iter);

    // Print the inode
    JSONFormatter jf(true);
    jf.open_object_section("inode");
    parent_ino.dump(&jf);
    jf.close_section();
    jf.flush(std::cout);
    std::cout << std::endl;
  }
}


/**
 * Given the ID of a directory fragment, iterate
 * over the dentrys in the fragment and call traverse_dentry
 * for each one.
 */
void InoUtility::traverse_fragment(object_t const &fragment)
{
  std::string start_after;

  std::map<std::string, bufferlist> dname_to_dentry;
  do {
    dname_to_dentry.clear();
    int rc = md_ioctx.omap_get_vals(fragment.name, start_after, "", 1024, &dname_to_dentry);
    if (rc != 0) {
      std::cout << "Error reading fragment object '" << fragment.name << "'" << std::endl;
      return;
    }

    for(std::map<std::string, bufferlist>::iterator i = dname_to_dentry.begin();
	i != dname_to_dentry.end(); ++i) {
      std::string const &dname = i->first;
      bufferlist &dentry_data = i->second;

      inode_t inode;
      fragtree_t fragtree;

      bufferlist::iterator dentry_iter = dentry_data.begin();
      snapid_t dentry_snapid;
      ::decode(dentry_snapid, dentry_iter);
      char type;
      ::decode(type, dentry_iter);
      if (type != 'I') {
        dout(0) << "Unknown type '" << type << "'" << dendl;
        assert(type == 'I'); // TODO: handle is_remote branch
      }
      ::decode(inode, dentry_iter);
      if (inode.is_symlink()) {
        string symlink;
        ::decode(symlink, dentry_iter);
      }
      ::decode(fragtree, dentry_iter);

      if (inode.is_dir()) {
	std::cout << "Directory '" << dname << "' " << std::hex << inode.ino << std::dec << std::endl;
	traverse_dir(fragtree, inode);
      } else {
	std::cout << "File '" << dname << "' " << std::hex << inode.ino << std::dec << std::endl;
      }

      start_after = dname;
    }

  } while (dname_to_dentry.size() != 0);
}


/**
 * Given a directory inode and its fragment tree, iterate over
 * the fragments and call traverse_fragment for each one.
 */
void InoUtility::traverse_dir(fragtree_t const &frags, inode_t const &ino)
{
  std::list<frag_t> frag_leaves;
  frags.get_leaves(frag_leaves);

  for(std::list<frag_t>::iterator i = frag_leaves.begin(); i != frag_leaves.end(); ++i) {
    object_t frag_obj = CInode::get_object_name(ino.ino, *i, "");
    traverse_fragment(frag_obj);
  }
}


void InoUtility::simple_fsck()
{
  traverse_dir(root_fragtree, root_ino);
}


