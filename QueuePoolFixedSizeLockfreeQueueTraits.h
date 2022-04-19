#ifndef    QUEUEPOOL_FIXEDSIZELOCKFREEQUEUETRAITS_H_PROTECTOR_WBOFE4J61ZFTKTXB0
#define    QUEUEPOOL_FIXEDSIZELOCKFREEQUEUETRAITS_H_PROTECTOR_WBOFE4J61ZFTKTXB0

#include "QueuePoolIConsumer.h"

/**
** @file QueuePoolFixedSizeLockfreeQueueTraits.h
** @author Astapov K.A.
**/

#include <boost/lockfree/queue.hpp>

namespace mapped_queue {

template < 
    typename KeyTn, 
    typename ValueTn, 
    size_t CapacityTn>
  struct FixedSizeLockfreeQueueTraits {
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

  typedef boost::lockfree::queue<
      QueueNode,
      boost::lockfree::fixed_sized<true>,
      boost::lockfree::capacity<Capacity> > Queue;

  static bool Enqueue( Queue& queue, const Key& key_value, const Value& the_value ) {
    return queue.bounded_push(   QueueNode( key_value, the_value )   );
  }
  static std::optional<QueueNode> Dequeue( Queue& queue ) {
    std::optional<QueueNode> ret;
    ret.emplace(); // @todo постараться убрать лишний вызов конструктора по умолчанию
    if (   !queue.pop( ret.value() )   ) {
      ret.reset();
    }
    return ret; // единственный return важен для оптимизации RVO/NRVO
  }
};

} // namespace mapped_queue


#endif  // QUEUEPOOL_FIXEDSIZELOCKFREEQUEUETRAITS_H_PROTECTOR_WBOFE4J61ZFTKTXB0
