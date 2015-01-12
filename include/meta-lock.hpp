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

#ifndef lc_meta_lock_hpp
#define lc_meta_lock_hpp

#include "locks.hpp"
#include "object-proxy.hpp"

namespace lc {


class meta_lock_base;
typedef std::shared_ptr <meta_lock_base> shared_meta_lock;


/*! \class meta_lock_base
 *  \brief Base class for \ref meta_lock.
 */

class meta_lock_base {
public:
  typedef meta_lock_write_proxy     write_proxy;
  typedef meta_lock_read_proxy      read_proxy;
  typedef lock_auth_base::auth_type auth_type;

  virtual write_proxy get_write_auth(auth_type &authorization, bool block = true);
  virtual read_proxy  get_read_auth(auth_type &authorization,  bool block = true);

  virtual inline ~meta_lock_base() {}

protected:
  virtual write_proxy get_write_auth(lock_auth_base *authorization, bool block = true) = 0;
  virtual read_proxy  get_read_auth(lock_auth_base *authorization,  bool block = true) = 0;

private:
  template <class> friend class locking_container_base;

  virtual lock_base *get_lock_object() = 0;
};


/*! \class meta_lock
 *  \brief Empty container, used as a global meta-locking mechanism.
 */

class meta_lock : public meta_lock_base {
private:
  typedef lock_auth <rw_lock> auth_base_type;

public:
  typedef meta_lock_base base;
  using base::write_proxy;
  using base::read_proxy;
  using base::auth_type;
  using base::get_write_auth;

  inline meta_lock() {}

private:
  meta_lock(const meta_lock&);
  meta_lock &operator = (const meta_lock&);

  inline write_proxy get_write_auth(lock_auth_base *authorization, bool block = true) {
    return write_proxy(true, &locks, authorization, false, block, NULL);
  }

  inline read_proxy get_read_auth(lock_auth_base *authorization, bool block = true) {
    return read_proxy(true, &locks, authorization, true, block, NULL);
  }

  inline lock_base *get_lock_object() {
    return &locks;
  }

  rw_lock locks;
};

} //namespace lc

#endif //lc_meta_lock_hpp
