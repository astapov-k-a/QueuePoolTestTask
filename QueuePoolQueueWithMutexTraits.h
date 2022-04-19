#ifndef    QUEUEPOOL_QUEUEWITHMUTEXTRAITS_H_PROTECTOR_TD2CEOQH91TZJSL9RNOQ0M593
#define    QUEUEPOOL_QUEUEWITHMUTEXTRAITS_H_PROTECTOR_TD2CEOQH91TZJSL9RNOQ0M593
/**
 ** @file QueuePoolQueueWithMutexTraits.h
 ** @author Astapov K.A.
 **/

#include "QueuePoolIConsumer.h"
#include <mutex>
#include <list>


namespace mapped_queue {

template < 
    typename KeyTn, 
    typename ValueTn, 
    size_t CapacityTn>
  struct QueueWithMutexTraits {
  constexpr static const size_t Capacity = CapacityTn;

  typedef KeyTn Key;
  typedef ValueTn Value;
  typedef std::shared_ptr< IConsumer<Key, Value> > Listener;

  struct QueueNode {
    QueueNode() {}
    QueueNode(
      const Key& key_value,
      const Value& the_value )
      :  key( key_value ),
      value( the_value ) {
    }
    Key key;
    Value value;
  };

  struct Queue {
    std::list<QueueNode> data;
    std::mutex queue_mutex;
  };
  static bool Enqueue( Queue& queue, const Key& key_value, const Value& the_value ) {
    std::lock_guard<std::mutex> locker(queue.queue_mutex);
    auto& data = queue.data;
    data.emplace_back(key_value, the_value);
    return 1;
  }
  static std::optional<QueueNode> Dequeue( Queue& queue ) {
    std::optional<QueueNode> ret;
    std::lock_guard<std::mutex> locker(queue.queue_mutex);
    auto& data = queue.data;
    if ( !data.empty() ) {
      ret.emplace( data.front() );
      //if ( data.front().value == 20002 ) {
      //  int x = 0;
      //}
      data.pop_front();
    }
    return std::move(ret);
  }
};

} // namespace mapped_queue


#endif  // QUEUEPOOL_QUEUEWITHMUTEXTRAITS_H_PROTECTOR_TD2CEOQH91TZJSL9RNOQ0M593
