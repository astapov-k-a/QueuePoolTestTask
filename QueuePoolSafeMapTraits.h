#ifndef    QUEUEPOOL_SAFEMAPTRAITS_H_PROTECTOR_IMM4AJ1F1QEZ3WWOPOJ2PC30K4QWDP8F
#define    QUEUEPOOL_SAFEMAPTRAITS_H_PROTECTOR_IMM4AJ1F1QEZ3WWOPOJ2PC30K4QWDP8F

#include "QueuePoolIConsumer.h"

/**
** @file QueuePoolSafeMapTraits.h
** @author Astapov K.A.
**/

#include "SafeUnorderedMap.h"

namespace mapped_queue {
  
template < 
    typename KeyTn, 
    typename ValueTn, 
    size_t CapacityTn >
  struct SafeMapTraits {
  constexpr static const size_t Capacity = CapacityTn;

  typedef KeyTn Key;
  typedef ValueTn Value;
  typedef std::shared_ptr< IConsumer<Key, Value> > Listener;
  typedef SafeUnorderedMap< Key, Listener > Map;

  static void AddToMap(
    Map& map,
    const Key& key_value,
    const Listener& listener_value) {
    auto& data = map;
    data.Set( key_value, listener_value );
  }
  static void ExcludeFromMap( const Map& map, const Key& key_value ) {
    std::lock_guard<std::mutex> locker( map.map_mutex );
    auto& data = map.data;
    auto& data = map;
    data.erase(key_value);
  }
  static Listener GetFromMap( Map& map, const Key& key_value ) {
    return *map.GetAndCreate( key_value );
  }
};
} // namespace mapped_queue


#endif  // QUEUEPOOL_SAFEMAPTRAITS_H_PROTECTOR_IMM4AJ1F1QEZ3WWOPOJ2PC30K4QWDP8F


