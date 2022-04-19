#ifndef    EVENT_H_PROTECTOR_SIGNATURE_IHW9OMK8EU1EGUGEZMIQBQMPJEQA75KZY8D0QEZ6
#define    EVENT_H_PROTECTOR_SIGNATURE_IHW9OMK8EU1EGUGEZMIQBQMPJEQA75KZY8D0QEZ6
/**
** @file Event.h
** @author Astapov K.A.
**/

#include <mutex>


//namespace nm {


class Event {
public:
  void NotifyOne() { cond_var_.notify_one(); }
  void NotifyAll() { cond_var_.notify_all(); }
  template <typename Callback> void Wait( Callback && callback ) {
    std::unique_lock<std::mutex> locker( cond_var_ );
    cond_var_.wait( locker, callback );
  }

private:
  std::condition_variable cond_var_;
  std::mutex mutx_;
};

//} // namespace nm


#endif  // EVENT_H_PROTECTOR_SIGNATURE_IHW9OMK8EU1EGUGEZMIQBQMPJEQA75KZY8D0QEZ6