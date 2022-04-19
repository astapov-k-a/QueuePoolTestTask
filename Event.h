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
  Event() : notify_flag_( false ) {}
  void NotifyOne() { 
    notify_flag_ = true;
    cond_var_.notify_one();
  }
  void NotifyAll() {
    notify_flag_ = true;
    cond_var_.notify_all();
  }
  template <typename Callback> void Wait( Callback && callback ) {
    std::unique_lock<std::mutex> locker( mutx_ );
    std::atomic_bool * notify_flag_ptr = &notify_flag_;
    cond_var_.wait( locker, [notify_flag_ptr, &callback]() { 
      bool re = ( notify_flag_ptr->load() ) && callback(); // false если надо продолжить ожидание
      return re;
    } );
  }
  void Wait() {
    Wait(   [](){ return 1; }   );
  }

private:
  std::condition_variable cond_var_;
  std::mutex mutx_;
  std::atomic_bool notify_flag_;
};

//} // namespace nm


#endif  // EVENT_H_PROTECTOR_SIGNATURE_IHW9OMK8EU1EGUGEZMIQBQMPJEQA75KZY8D0QEZ6