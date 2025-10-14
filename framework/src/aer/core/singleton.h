#ifndef AER_CORE_SINGLETON_H_
#define AER_CORE_SINGLETON_H_

#include <cassert>
#include <utility>

/* -------------------------------------------------------------------------- */

template <class T>
class Singleton // : public non_copyable<Singleton<T>>
                // , public non_movable<Singleton<T>>
{
 public:
  template<class... U>
  static void Initialize(U&&... u) {
    assert(sInstance == nullptr);
    sInstance = new T(std::forward<U>(u)...);
  }

  static
  void Deinitialize() {
    if (sInstance != nullptr) {
      delete sInstance;
      sInstance = nullptr;
    }
  }

  static
  T& Get() {
    assert(sInstance != nullptr);
    return *sInstance;
  }

 protected:
  Singleton() = default;
  virtual ~Singleton() = default;

 private:
  static T* sInstance;
};

template<class T> T* Singleton<T>::sInstance = nullptr;

/* -------------------------------------------------------------------------- */

#endif  // AER_CORE_SINGLETON_H_
