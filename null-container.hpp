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

#ifndef null_container_hpp
#define null_container_hpp

#include "locks.hpp"
#include "object-proxy.hpp"


/*! \class null_container_base
    \brief Base class for \ref null_container.
 */

class null_container_base {
public:
  typedef object_proxy <void>       proxy;
  typedef lock_auth_base::auth_type auth_type;

  virtual proxy get_auth(auth_type &authorization, bool block = true) {
    return this->get_auth(authorization.get(), block);
  }

  virtual proxy get_auth(lock_auth_base *authorization, bool block = true) = 0;

  virtual inline ~null_container_base() {}

private:
  template <class> friend class locking_container_base;

  virtual lock_base *get_lock_object() = 0;
};


/*! \class null_container
    \brief Empty container, used as a global multi-locking mechanism.
 */

class null_container : public null_container_base {
private:
  typedef lock_auth <rw_lock> auth_base_type;

public:
  typedef null_container_base base;
  using base::proxy;
  using base::auth_type;

  ~null_container() {
    this->get_auth(NULL);
  }

  inline proxy get_auth(auth_type &authorization, bool block = true) {
    return this->get_auth(authorization.get(), block);
  }

  inline proxy get_auth(lock_auth_base *authorization, bool block = true) {
    return proxy(true, &locks, authorization, block, NULL);
  }

private:
  inline lock_base *get_lock_object() {
    return &locks;
  }

  rw_lock locks;
};

#endif //null_container_hpp
