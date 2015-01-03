#ifndef null_container_hpp
#define null_container_hpp


/*! \class null_container_base
    \brief Base class for \ref null_container.
 */

class null_container_base {
public:
  typedef object_proxy <void>       proxy;
  typedef lock_auth_base::auth_type auth_type;

  virtual proxy get_auth(auth_type &Authorization, bool Block = true) {
    return this->get_auth(Authorization.get(), Block);
  }

  virtual proxy get_auth(lock_auth_base *Authorization, bool Block = true) = 0;

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

  inline proxy get_auth(auth_type &Authorization, bool Block = true) {
    return this->get_auth(Authorization.get(), Block);
  }

  inline proxy get_auth(lock_auth_base *Authorization, bool Block = true) {
    return proxy(true, &locks, Authorization, Block, NULL);
  }

private:
  inline lock_base *get_lock_object() {
    return &locks;
  }

  rw_lock locks;
};

#endif //null_container_hpp
