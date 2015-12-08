#ifndef __RECORDER_H__
#define __RECORDER_H__

#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

using namespace std;

// A class to do inter-task sync.
template <typename T>
class Recorder {
 public:
  explicit Recorder(size_t maxElems) : maxElems_(maxElems) {
    currElems_.store(0);
    elements_.reset(new T[maxElems]);
  }

  ~Recorder() {}

  inline size_t NumberElements() {
    return currElems_.load();
  }

  inline T* Elements() {
    return elements_.get();
  }

  inline bool Add(T val) {
    size_t pos = currElems_++;

    if (pos >= maxElems_) {
      currElems_--;
      return false;
    } else {
      elements_.get()[pos] = val;
      return true;
    }
  }

  inline void Sort() {
    std::sort(elements_.get(), elements_.get() + NumberElements());
  }

  // Array to save objs.
  std::unique_ptr<T> elements_;

  // Size of above array.
  size_t maxElems_;

  // How many elems are recorded in the above array.
  std::atomic<size_t> currElems_;
};



#endif  // __RECORDER_H__
