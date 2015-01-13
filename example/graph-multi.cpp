/* This software is released under the BSD License.
 |
 | Copyright (c) 2015, Kevin P. Barry [ta0kira@gmail.com]
 | All rights reserved.
 |
 | Redistribution  and  use  in  source  and   binary  forms,  with  or  without
 | modification, are permitted provided that the following conditions are met:
 |
 | - Redistributions of source code must retain the above copyright notice, this
 |   list of conditions and the following disclaimer.
 |
 | - Redistributions in binary  form must reproduce the  above copyright notice,
 |   this list  of conditions and the following disclaimer in  the documentation
 |   and/or other materials provided with the distribution.
 |
 | - Neither the name  of the  Locking Container Project  nor  the names  of its
 |   contributors may be  used to endorse or promote products  derived from this
 |   software without specific prior written permission.
 |
 | THIS SOFTWARE IS  PROVIDED BY THE COPYRIGHT HOLDERS AND  CONTRIBUTORS "AS IS"
 | AND ANY  EXPRESS OR IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT LIMITED TO, THE
 | IMPLIED WARRANTIES OF  MERCHANTABILITY  AND FITNESS FOR A  PARTICULAR PURPOSE
 | ARE DISCLAIMED.  IN  NO EVENT SHALL  THE COPYRIGHT  OWNER  OR CONTRIBUTORS BE
 | LIABLE  FOR  ANY  DIRECT,   INDIRECT,  INCIDENTAL,   SPECIAL,  EXEMPLARY,  OR
 | CONSEQUENTIAL   DAMAGES  (INCLUDING,  BUT  NOT  LIMITED  TO,  PROCUREMENT  OF
 | SUBSTITUTE GOODS OR SERVICES;  LOSS  OF USE,  DATA,  OR PROFITS;  OR BUSINESS
 | INTERRUPTION)  HOWEVER  CAUSED  AND ON  ANY  THEORY OF LIABILITY,  WHETHER IN
 | CONTRACT,  STRICT  LIABILITY, OR  TORT (INCLUDING  NEGLIGENCE  OR  OTHERWISE)
 | ARISING IN ANY  WAY OUT OF  THE USE OF THIS SOFTWARE, EVEN  IF ADVISED OF THE
 | POSSIBILITY OF SUCH DAMAGE.
 +~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

/* This is a complex working example of a design pattern for a graph structure.
 * The overall design pattern used here is as follows:
 *
 *   - The primary data structure is a graph, where each node in the graph is
 *     protected by its own lock. Edges are stored within each node using a
 *     table of pointers to other protected nodes.
 *
 *   - All nodes are referenced using shared pointers to protected nodes. This
 *     has its positive and negative points. One positive point is that it
 *     allows reference to a node without having to hold a lock on any object.
 *     One negative point is that a node might no longer be a part of the graph
 *     when a thread finally gets around to accessing it.
 *
 *   - To move from one node to another, a write lock is obtained for the
 *     current node (allowing non-'const' access to its edges), and the next
 *     node is found in the current node's list of edges. (See below for a
 *     suggested design pattern.)
 *
 *   - The entire graph is managed by a graph object. This object contains a
 *     table of pointers to all of the graph's nodes. This table merely serves
 *     as an entry point to the graph; once a thread finds a node in that table,
 *     it can traverse the graph without needing to further consider the table.
 *     The table itself is protected by a lock; however, a thread only needs to
 *     obtain a lock when searching or modifying the table. The table doesn't
 *     need to be locked to perform operations on the graph itself.
 *
 *   - There is a single meta-lock that corresponds to the graph. This lock is
 *     used when a thread needs to lock multiple nodes in an arbitrary order.
 *
 *   - The nodes in the graph are protected by ordered locks. Ideally, each node
 *     in the graph will have a distinct order. That way, if a thread needs to
 *     lock two specific nodes (e.g., to add or delete an edge), it can lock the
 *     node with the lower order first, preventing potential deadlocks.
 *
 * Things I still need to implement here:
 *
 *   - Add some threads that move around the graph and perform various
 *     operations. The main thread already demonstrates that the design pattern
 *     works when no other threads hold locks on nodes; this example needs to be
 *     extended to demonstrate that it works when arbitrary nodes are locked.
 *     (It almost certainly won't deadlock; however, it still needs to be
 *     designed to succeed in the face of rejected locks.)
 *
 *   - I need to come up with a better design pattern for moving from one node
 *     to another without a spinlock. One option is to: 1) obtain a write lock
 *     on the current node; 2) copy its list of edges; 3) unlock the current
 *     node; 4) preview/select a destination node from the copied list. This
 *     obviates the need for holding multiple locks at once. Note that even if
 *     an edge is deleted during step 4, the destination node will always still
 *     exist. The trade-off is accepting that the graph might be modified during
 *     the operation. (Another perspective is that it makes no difference
 *     whether or not the edge is removed before or after the move, as long as
 *     the move succeeds in both cases.)
 *
 * Suggested compilation command:
 *   c++ -Wall -pedantic -std=c++11 -O2 -I../include graph-multi.cpp -o graph-multi -lpthread
 */

#include <set>
#include <map>
#include <queue>
#include <string>
#include <memory>
#include <utility>
#include <cassert>
#include <iostream>

#include <stdio.h>

#include "locking-container.hpp"
//(necessary for non-template source)
#include "locking-container.inc"


typedef lc::lock_auth_base::auth_type  auth_type;
typedef lc::lock_auth_base::order_type order_type;
typedef lc::shared_meta_lock           shared_meta_lock;


template <class Type>
struct graph_node {
  typedef Type stored_type;
  typedef lc::locking_container_base <graph_node> protected_node;
  typedef std::shared_ptr <protected_node>        shared_node;
  typedef std::set <shared_node>                  connected_nodes;

  inline graph_node(const stored_type &value) : obj(value) {}

  inline graph_node(stored_type &&value = stored_type()) : obj(std::move(value)) {}

  inline graph_node(graph_node &&other) : out(std::move(other.out)),
    in(std::move(other.in)), obj(std::move(other.obj)) {}

private:
  graph_node(const graph_node&);
  graph_node &operator = (const graph_node&);

public:
  static inline bool connect_nodes(shared_node left, shared_node right,
    auth_type auth = auth_type(), shared_meta_lock master_lock = shared_meta_lock(),
    bool try_multi = true) {
    return change_connection_common(&insert_edge, left, right, auth, master_lock, try_multi);
  }

  static inline bool disconnect_nodes(shared_node left, shared_node right,
    auth_type auth = auth_type(), shared_meta_lock master_lock = shared_meta_lock(),
    bool try_multi = true) {
    return change_connection_common(&erase_edge, left, right, auth, master_lock, try_multi);
  }

  //NOTE: this might never get called if there's a circular reference!
  virtual inline ~graph_node() {}

  connected_nodes out, in;
  stored_type     obj;

protected:
  static void insert_edge(connected_nodes &left, const shared_node &right) {
    left.insert(right);
  }

  static void erase_edge(connected_nodes &left, const shared_node &right) {
    left.erase(right);
  }

  static void get_two_writes(shared_node left, shared_node right, auth_type auth,
    shared_meta_lock master_lock, typename protected_node::write_proxy &write1,
    typename protected_node::write_proxy &write2, bool block = true) {
    assert(left.get() && right.get());
    bool order = left->get_order() < right->get_order();
    if (master_lock && auth) {
      if (order) {
        write1 = left->get_write_multi(*master_lock, auth, block);
        write2 = right->get_write_multi(*master_lock, auth, block);
      } else {
        write2 = right->get_write_multi(*master_lock, auth, block);
        write1 = left->get_write_multi(*master_lock, auth, block);
      }
    } else if (auth) {
      if (order) {
        write1 = left->get_write_auth(auth, block);
        write2 = right->get_write_auth(auth, block);
      } else {
        write2 = right->get_write_auth(auth, block);
        write1 = left->get_write_auth(auth, block);
      }
    } else {
      write1 = left->get_write(block);
      write2 = right->get_write(block);
    }
  }

  template <class Func>
  static bool change_connection_common(Func func, shared_node left, shared_node right,
    auth_type auth = auth_type(), shared_meta_lock master_lock = shared_meta_lock(),
    bool try_multi = true) {
    lc::meta_lock::write_proxy multi;
    if (try_multi && master_lock && !(multi = master_lock->get_write_auth(auth))) return false;

    typename protected_node::write_proxy write_l, write_r;
    get_two_writes(left, right, auth, master_lock, write_l, write_r);
    multi.clear();
    if (!write_l || !write_r) return false;

    (*func)(write_l->out, right);
    (*func)(write_r->in,  left);

    return true;
  }
};


template <class Type>
struct graph_head {
  typedef graph_node <Type>             node;
  typedef typename node::stored_type    stored_type;
  typedef typename node::protected_node protected_node;
  typedef typename node::shared_node    shared_node;

  virtual shared_node get_graph_head(auth_type auth) = 0;

  virtual typename lc::meta_lock_base::write_proxy get_master_lock(auth_type auth)   = 0;
  virtual typename lc::meta_lock_base::read_proxy  block_master_lock(auth_type auth) = 0;
  virtual shared_meta_lock show_master_lock() = 0;

  virtual inline ~graph_head() {}
};


template <class Index, class Type>
class graph : public graph_head <Type> {
public:
  typedef graph_head <Type> base;
  using typename base::node;
  using typename base::stored_type;
  using typename base::protected_node;
  using typename base::shared_node;
  typedef Index                                       index_type;
  typedef std::map <index_type, shared_node>          node_map;
  typedef typename node_map::iterator                 iterator;
  typedef lc::ordered_lock <lc::rw_lock>              lock_type;
  typedef lc::locking_container <node_map, lock_type> protected_node_map;

  graph(order_type o) : master_lock(new lc::meta_lock), all_nodes(node_map(), o) {}

private:
  graph(const graph&);
  graph &operator = (const graph&);

public:
  shared_node get_graph_head(auth_type auth) {
    typename protected_node_map::write_proxy write = all_nodes.get_write_multi(*master_lock, auth);
    if (!write) return shared_node();
    return write->size()? write->begin()->second : shared_node();
  }

  typename lc::meta_lock_base::write_proxy get_master_lock(auth_type auth) {
    return master_lock? master_lock->get_write_auth(auth) : lc::meta_lock_base::write_proxy();
  }

  typename lc::meta_lock_base::read_proxy block_master_lock(auth_type auth) {
    return master_lock? master_lock->get_read_auth(auth) : lc::meta_lock_base::read_proxy();
  }

  shared_meta_lock show_master_lock() {
    assert(master_lock.get());
    return master_lock;
  }

  inline order_type get_order() const {
    return all_nodes.get_order();
  }

  template <class Func, class ... Args>
  bool iterate_nodes(auth_type auth, Func func, Args ... args) {
    typename protected_node_map::write_proxy write = all_nodes.get_write_multi(*master_lock, auth);
    if (!write) return false;
    //NOTE: 'master_lock' isn't used before because we want to finish the loop
    //without exiting early for another thread's multi-lock request
    for (iterator current = write->begin(), end = write->end();
         current != end; ++current) {
      assert(current->second.get());
      //NOTE: if ordering is respected, this should always block
      typename protected_node::read_proxy this_node = current->second->get_read_auth(auth);
      if (!this_node) return false;
      (*func)(current->first, *this_node, args...);
    }
    return true;
  }

  virtual bool connect_nodes(shared_node left, shared_node right, auth_type auth,
    bool try_multi = true) {
    //NOTE: this doesn't use 'find_node' so that error returns only pertain to
    //failed lock operations
    return node::connect_nodes(left, right, auth, try_multi? master_lock : shared_meta_lock());
  }

  virtual bool disconnect_nodes(shared_node left, shared_node right, auth_type auth,
    bool try_multi = true) {
    //NOTE: this doesn't use 'find_node' so that error returns only pertain to
    //failed lock operations
    return node::disconnect_nodes(left, right, auth, try_multi? master_lock : shared_meta_lock());
  }

  virtual shared_node find_node(const index_type &index, auth_type auth) {
    assert(master_lock.get());
    typename protected_node_map::write_proxy write = all_nodes.get_write_multi(*master_lock, auth);
    if (!write) return shared_node();
    typename node_map::iterator found = write->find(index);
    return (found == write->end())? shared_node() : found->second;
  }

  virtual bool insert_node(const index_type &index, shared_node value, auth_type auth) {
    assert(value.get());
    //NOTE: added nodes must have higher order than the node map itself
    assert(value->get_order() > all_nodes.get_order());
    return this->change_node(index, auth, &replace_node, value);
  }

  virtual bool erase_node(const index_type &index, auth_type auth) {
    return this->change_node(index, auth, &remove_node);
  }

  virtual ~graph() {
    auth_type auth(all_nodes.get_new_auth());
    typename protected_node_map::write_proxy write = all_nodes.get_write_auth(auth, false);
    assert(write);
    for (typename node_map::iterator current = write->begin(), end = write->end();
         current != end; ++current) {
      assert(current->second.get());
      //NOTE: if it's already locked, that's a serious problem here
      //NOTE: auth. is only used to appease ordered locks
      typename node::protected_node::write_proxy this_node = current->second->get_write_auth(auth, false);
      assert(this_node);
      //NOTE: doing this prevents a circular reference memory leak
      this_node->out.clear();
      this_node->in.clear();
    }
  }

protected:
  static void replace_node(node_map &the_nodes, const index_type &index, shared_node value) {
    the_nodes[index] = value;
  }

  static void remove_node(node_map &the_nodes, const index_type &index) {
    the_nodes.erase(index);
  }

  template <class Member>
  bool remove_edges(shared_node value, Member remove_left, Member remove_right, auth_type auth) {
    assert(master_lock.get());
    typename node::protected_node::write_proxy left = value->get_write_multi(*master_lock, auth);
    if (!left) return false;
    for (typename node::connected_nodes::iterator
         current = (left->*remove_left).begin(), end = (left->*remove_left).end();
         current != end; ++current) {
      assert(current->get());
      typename node::protected_node::write_proxy right = (*current)->get_write_multi(*master_lock, auth);
      if (!right) return false;
      (right->*remove_right).erase(value);
    }
    return true;
  }

  template <class Func, class ... Args>
  bool change_node(const index_type &index, auth_type auth, Func func, Args ... args) {
    assert(master_lock.get());
    shared_node old_node = this->find_node(index, auth);
    //NOTE: this is in the outer scope so the lock is continuous
    lc::meta_lock::write_proxy protect_write;
    if (old_node) {
      //(boot off all other locks)
      protect_write = this->get_master_lock(auth);
      if (!protect_write) return false;
      //NOTE: these should never fail if 'master_lock' is used properly
      if (!this->remove_edges(old_node, &node::out, &node::in, auth)) return false;
      if (!this->remove_edges(old_node, &node::in, &node::out, auth)) return false;
    }
    typename protected_node_map::write_proxy write = all_nodes.get_write_multi(*master_lock, auth);
    if (!write) return false;
    //NOTE: if this results in destruction of the old node, it shouldn't have
    //any locks on it that will cause problems
    (*func)(*write, index, args...);
    return true;
  }

private:
  shared_meta_lock   master_lock;
  protected_node_map all_nodes;
};


template <class Type>
static const Type &identity(const Type &value) {
  return value;
}


template <class Type, class Result = const Type&>
static bool print_graph(graph_head <Type> &the_graph, auth_type auth,
  Result(*convert)(const Type&) = &identity <Type>) {
  typedef graph_head <Type> graph_type;
  typedef std::queue <typename graph_type::protected_node::write_proxy> proxy_queue;
  proxy_queue locked, pending;

  lc::meta_lock_base::write_proxy multi = the_graph.get_master_lock(auth);
  if (!multi) return false;

  typename graph_type::shared_node head = the_graph.get_graph_head(auth);
  if (!head) return true;

  typename graph_type::protected_node::write_proxy next =
    head->get_write_multi(*the_graph.show_master_lock(), auth, false);
  //(nothing should be locked at this point)
  if (!next) return false;
  locked.push(next);

  std::cout << (*convert)(next->obj) << " (first node)" << std::endl;

  while (next) {
    for (typename graph_type::node::connected_nodes::iterator
         current = next->out.begin(), end = next->out.end();
         current != end; ++current) {
      assert(current->get());
      typename graph_type::protected_node::write_proxy write =
        (*current)->get_write_multi(*the_graph.show_master_lock(), auth, false);
      //NOTE: this should only happen if we already have the lock
      if (!write) continue;

      std::cout << (*convert)(write->obj) << " (first seen from "
                << (*convert)(next->obj)  << ")" << std::endl;

      pending.push(write);
      locked.push(write);
    }

    next = pending.size()? pending.front() : typename graph_type::protected_node::write_proxy();
    if (next) pending.pop();
  }

  return true;
}


template <class Index, class Type, class Result = const Type&>
static void print_node(const Index &index, const graph_node <Type> &the_node,
  Result(*convert)(const Type&) = &identity <Type>) {
    std::cout << "node " << index
              << "(in: "   << the_node.in.size()
              << ", out: " << the_node.out.size() << "): "
              << (*convert)(the_node.obj) << std::endl;
}


struct tagged_value {
  tagged_value(int t, int v = 0) : tag(t), value(v) {}

  static int get_tag(const tagged_value &object) {
    return object.tag;
  }

  const int tag;
  int       value;
};


typedef graph <int, tagged_value> int_graph;
//NOTE: using an ordered lock allows adding/removing edges without using the master lock
typedef lc::locking_container <int_graph::node, lc::ordered_lock <lc::rw_lock> > locking_node;


int main() {
  int       graph_size = 10;
  int_graph main_graph(1);
  auth_type main_auth(locking_node::new_auth());

  for (int i = 0; i < graph_size; i++) {
    //NOTE: lock order must be greater than that of 'main_graph'
    order_type lock_order = main_graph.get_order() + i + 1;
    if (!main_graph.insert_node(i, int_graph::shared_node(new locking_node(tagged_value(i), lock_order)), main_auth)) {
      fprintf(stderr, "could not add node %i\n", i);
      return 1;
    } else {
      fprintf(stderr, "added node %i\n", i);
    }
  }

  for (int i = 0; i < graph_size; i++) {
    int from = i, to = (i + 1) % graph_size;
    int_graph::shared_node left  = main_graph.find_node(from, main_auth);
    int_graph::shared_node right = main_graph.find_node(to,   main_auth);
    if (!left || !right) {
      fprintf(stderr, "error finding nodes\n");
      return 1;
    }
    //NOTE: 'false' means we don't wait for a multi-lock (because of ordered locks)
    if (!main_graph.connect_nodes(left, right, main_auth, false)) {
      fprintf(stderr, "could not connect node %i to node %i\n", from, to);
      return 1;
    } else {
      fprintf(stderr, "connected node %i to node %i\n", from, to);
    }
  }

  print_graph(main_graph, main_auth, &tagged_value::get_tag);

  for (int i = 0; i < graph_size; i++) {
    int remove = i;
    if (!main_graph.erase_node(remove, main_auth)) {
      fprintf(stderr, "could not erase node %i\n", remove);
      return 1;
    } else {
      fprintf(stderr, "erased node %i\n", remove);
      main_graph.iterate_nodes(main_auth, &print_node <int, tagged_value, int>, &tagged_value::get_tag);
    }
  }
}
