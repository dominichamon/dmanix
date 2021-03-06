#ifndef FS_NODE_H
#define FS_NODE_H

#include <stdint.h>

namespace fs {

struct DirEntry;

// TODO(dominic): Make this an interface so open et al can be specialized
// instead of using dispatch to fn ptrs.
class Node {
 public:
  typedef uint32_t (*read_func)(Node* node, uint32_t offset, uint32_t size,
                                uint8_t* buffer);
  typedef uint32_t (*write_func)(Node* node, uint32_t offset, uint32_t size,
                                 uint8_t* buffer);
  typedef void (*open_func)(Node* node);
  typedef void (*close_func)(Node* node);
  typedef DirEntry* (*readdir_func)(Node* node, uint32_t index);
  typedef Node* (*finddir_func)(Node* node, const char* name);

  Node();
  explicit Node(const char* node_name, uint32_t flags);
  ~Node() { }

  uint32_t Read(uint32_t offset, uint32_t size, uint8_t* buffer);
  uint32_t Write(uint32_t offset, uint32_t size, uint8_t* buffer);
  void Open();
  void Close();
  DirEntry* ReadDir(uint32_t index);
  Node* FindDir(const char* name);

  char name[128];
  uint32_t perm;
  uint32_t uid;
  uint32_t gid;
  uint32_t flags;
  uint32_t inode;
  uint32_t length;
  uint32_t impl;

  read_func read;
  write_func write;
  open_func open;
  close_func close;
  readdir_func readdir;
  finddir_func finddir;

  // For mountpoints and symlinks
  Node* link;

 private:
  // these methods intentionally left unimplemented.
  Node(const Node&);
};

}  // namespace fs

#endif  // FS_NODE_H
