#ifndef    QUEUEPOOL_MAPWITHMUTEXTRAITS_H_PROTECTOR_G15MJURI2WXV8TUJ4SUGX4SGBWC
#define    QUEUEPOOL_MAPWITHMUTEXTRAITS_H_PROTECTOR_G15MJURI2WXV8TUJ4SUGX4SGBWC
/**
 ** @file QueuePoolMapWithMutexTraits.h
 ** @author Astapov K.A.
 **/

#include "QueuePoolIConsumer.h"
#include <mutex>
#include <map>

namespace mapped_queue {

template < 
    typename KeyTn, 
    typename ValueTn, 
    size_t CapacityTn >
struct MapWithMutexTraits {
  constexpr static const size_t Capacity = CapacityTn;

  typedef KeyTn Key;
  typedef ValueTn Value;
  typedef std::shared_ptr< IConsumer<Key, Value> > Listener;

  struct Map {
    std::map<Key, Listener> data;
    std::mutex map_mutex;
  };
  static void AddToMap(
    Map& map,
    const Key& key_value,
    const Listener& listener_value) {
    std::lock_guard<std::mutex> locker( map.map_mutex );
    auto& data = map.data;
    data[key_value] = listener_value;
  }
  static void ExcludeFromMap( const Map& map, const Key& key_value ) {
    std::lock_guard<std::mutex> locker( map.map_mutex );
    auto& data = map.data;
    data.erase(key_value);
  }
  static Listener GetFromMap( Map& map, const Key& key_value ) {
    const std::lock_guard<std::mutex> locker( map.map_mutex );
    auto& data = map.data;
    auto iter = data.find( key_value );
    if ( iter != data.end() ) {
      return iter->second;
    }
    return Listener();
  }
};

} // namespace mapped_queue


#endif  // QUEUEPOOL_MAPWITHMUTEXTRAITS_H_PROTECTOR_G15MJURI2WXV8TUJ4SUGX4SGBWC

