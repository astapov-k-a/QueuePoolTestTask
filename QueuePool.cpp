//#include <QCoreApplication>
#define QUEUE_WITH_MUTEX 0
#define SAFE_MAP 1
#define PROCESS_ERROR(Text) { printf("ERROR %s", Text); }

#if SAFE_MAP == 1
#  include "SafeUnorderedMap.h"
#endif
#include <boost/lockfree/queue.hpp>
#include <list>
#include <memory>
#include <array>
#include <mutex>
#include <type_traits>
#include <thread>
#include <optional>
#include <map>
#include <new>

#if __unix__
#   include <unistd.h>
#else
#   include <windows.h>
#endif


//#define PRDEBUG printf
#define PRDEBUG 


void TestSleep( size_t secs ) {
  using namespace std::chrono_literals;
  std::this_thread::sleep_for( secs * 1s );
}

namespace mapped_queue {

template <typename Key, typename Value>
struct IConsumer
{
  IConsumer() {}
  virtual void Consume( Key id, const Value& value ) {
    id;
    value;
  }
};


template < typename KeyTn, typename ValueTn, size_t CapacityTn >
struct DefaultTraits {
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

#if QUEUE_WITH_MUTEX == 0
  typedef boost::lockfree::queue<
      QueueNode,
      boost::lockfree::fixed_sized<true>,
      boost::lockfree::capacity<Capacity> > Queue;
#else
  struct Queue {
    std::list<QueueNode> data;
    std::mutex queue_mutex;
  };
  #endif

# if SAFE_MAP == 1
  typedef SafeUnorderedMap< Key, Listener > Map;
# else
  struct Map {
    std::map<Key, Listener> data;
    std::mutex map_mutex;
  };
# endif
  static bool Enqueue( Queue& queue, const Key& key_value, const Value& the_value ) {
#   if QUEUE_WITH_MUTEX != 0
    std::lock_guard<std::mutex> locker(queue.queue_mutex);
    auto& data = queue.data;
    data.emplace_back(key_value, the_value);
    return 1;
#   else
    return queue.bounded_push(   QueueNode( key_value, the_value )   );
#   endif
  }
  static std::optional<QueueNode> Dequeue( Queue& queue ) {
    std::optional<QueueNode> ret;
# if QUEUE_WITH_MUTEX != 0
    std::lock_guard<std::mutex> locker(queue.queue_mutex);
    auto& data = queue.data;
    if (!data.empty()) {
        ret.emplace(data.front());
        if ( data.front().value == 20002 ) {
          int x = 0;
        }
        data.pop_front();
    }
    return std::move(ret)
#   else
    ret.emplace();
    return queue.pop( ret.value() ) ? ret : std::optional<QueueNode>();
#   endif;
  }
  static void AddToMap(
      Map& map,
      const Key& key_value,
      const Listener& listener_value) {
#   if SAFE_MAP == 0
    std::lock_guard<std::mutex> locker( map.map_mutex );
    auto& data = map.data;
#   else
    auto& data = map;
#   endif
    data.Set( key_value, listener_value );
  }
static void ExcludeFromMap( const Map& map, const Key& key_value ) {
#   if SAFE_MAP == 0
    std::lock_guard<std::mutex> locker( map.map_mutex );
    auto& data = map.data;
#   else
    auto& data = map;
#   endif
    data.erase(key_value);
  }
  static Listener GetFromMap( Map& map, const Key& key_value ) {
#   if SAFE_MAP == 0
    const std::lock_guard<std::mutex> locker( map.map_mutex );
    auto& data = map.data;
    auto iter = data.find( key_value );
    if ( iter != data.end() ) {
        return iter->second;
    }
#   else
    return *map.GetAndCreate( key_value );
#   endif
    return Listener();
  }

};

template <
    typename KeyTn,
    typename ValueTn,
    size_t TotalCapacity,
    typename TraitsTn = DefaultTraits<KeyTn, ValueTn, TotalCapacity> >
class QueuePool {
 public:
  //constexpr static const size_t kTotalCapacity = TotalCapacityTn;

  typedef QueuePool<KeyTn, ValueTn, TotalCapacity, TraitsTn> This;
  typedef KeyTn Key;
  typedef ValueTn Value;
  typedef TraitsTn Traits;

  typedef typename Traits::Queue     Queue;
  typedef typename Traits::QueueNode QueueNode;
  typedef typename Traits::Map       Map;
  typedef typename Traits::Listener Listener;

  // Плохой тон - создавать поток в конструкторе. Поэтому делаем фабричный метод
  static This* Create() {
    printf("\nCreate start");
      This* ret = new (std::nothrow) This();
      if ( ret ) {
          ret->thrd_.reset( new (std::nothrow) std::thread([ret]() {
              ConsumerThreadFunctionStatic( ret->stop_command_.get_token(), ret );
              }) );
          ret->thrd_->detach();
      }
    printf( "\nCreate finished" );
    return ret;
  }
  void StopProcessing() {
      stop_command_.request_stop();
  }
  void Subscribe( const Key& key, const Listener& listener ) {
      Traits::AddToMap( map_, key, listener );
  }
  void Unsubscribe( const Key& key ) {
      Traits::ExcludeFromMap( map_, key );
  }
  void Enqueue( const Key& key_value, const Value& the_value ) {
      printf( "\nEnqueue %u %u", key_value, the_value );
      bool can_enqueue = Traits::Enqueue( queue_, key_value, the_value );
      if ( !can_enqueue ) {
        std::unique_lock<std::mutex> locker( condit_enqueue_mutex_ ); {
          auto& queue_local = queue_;
          printf("\nE!!!");
          enqueue_condvar_.wait( locker, [&queue_local, &key_value, &the_value]() {
            return !Traits::Enqueue( queue_local, key_value, the_value ); // false если надо продолжить ожидание
          });
        }
      }
      StartHasValueEvent();
  }

protected:
  QueuePool() {
  }
  void StartHasValueEvent() {
    consume_condvar_.notify_one();
  }
  void StartEndCapacityEvent() {
    enqueue_condvar_.notify_one();
  }
  void Dequeue() {
    PRDEBUG( "\nDequeue started" );
      auto node_optional = Traits::Dequeue( queue_ );
      if ( !node_optional.has_value() ) {
        PRDEBUG("\nDequeue freeze");
        std::unique_lock<std::mutex> locker( condit_consume_mutex_ ); {
          auto & queue_local = queue_;
          consume_condvar_.wait( locker, [&node_optional, &queue_local]() {
            node_optional = Traits::Dequeue( queue_local );
            return node_optional.has_value(); // false если надо продолжить ожидание
          } );
        }
      }
      PRDEBUG( "\nDequeue unfreeze" );
      const auto & node = node_optional.value();
      PRDEBUG("\nDequeue unfreeze value = %u %u", node. key, node.value );
      auto listener = Traits::GetFromMap( map_, node.key );
      PRDEBUG("\nDequeue unfreeze listener");
      if ( (bool)listener ) {
          auto& value = node.value;
          PRDEBUG("\nDequeue start consume");
          listener->Consume( node.key, value );
          StartEndCapacityEvent();
      }
      else {
          PROCESS_ERROR("Unknown queue");
      }
  }
  void ConsumerThreadFunction( std::stop_token stop_token ) {
    size_t debug_counter = 0;
    while (1) {
      if ( stop_token.stop_requested() ) return;
      Dequeue();
      ++debug_counter;
      //if (debug_counter == 2000)    TestSleep(25);
    }
  }

  static void ConsumerThreadFunctionStatic( std::stop_token stop_token, This* var ) {
    if ( var ) {
      var->ConsumerThreadFunction( stop_token );
    } else {
      PROCESS_ERROR("Parameter error");
    }
  }

 private:
  Queue queue_;
  Map map_;
  std::unique_ptr<std::thread> thrd_;
  std::stop_source stop_command_;
  std::mutex condit_consume_mutex_;
  std::condition_variable consume_condvar_;
  std::mutex condit_enqueue_mutex_;
  std::condition_variable enqueue_condvar_;
};

} // namespace mapped_queue


struct TestListener : public mapped_queue::IConsumer<int, int> {
  TestListener( size_t thread_id_number ) : thread_id_number_( thread_id_number ) {
  }

  virtual void Consume( int id, const int& value ) override
  {
    assert( id == thread_id_number_ );
    printf( "\nconsume %d, %d", id, value );
  }
 private:
  size_t thread_id_number_;
};


typedef mapped_queue::QueuePool< int, int, 65500> Pool;

int main(int argc, char* argv[])
{
  std::unique_ptr< Pool > pool ( Pool::Create() );
  constexpr const size_t kThreadSize = 4;
  for ( size_t i = 1; i <= kThreadSize; ++i ) {
    std::shared_ptr< mapped_queue::IConsumer<int,int> > listener = 
                                           std::make_shared<TestListener>( i );
    pool->Subscribe( i, listener );
  }

  using namespace std;
  vector< unique_ptr< jthread > > threads;
  constexpr const size_t kPushNumber = 1000;
  threads.resize( kThreadSize );
  atomic_int32_t counter = 1;
  atomic_int32_t thread_counter = 0;
  for ( auto & thread_ptr : threads ) {
    ++thread_counter;
    size_t current_thread = thread_counter;
    thread_ptr.reset( new jthread( [&pool, &counter, current_thread](){
        printf("\nthread reset");
        for ( size_t i = 0; i <= kPushNumber; ++i ) {
          //if ( i == kPushNumber/2 )    TestSleep(25);
          size_t selected_queue = current_thread;
          size_t current_counter = counter;
          printf( "\nenqueue call (thr=%u) {queue=%u val=%u} %u", current_thread, selected_queue, current_counter, i );
          pool->Enqueue( selected_queue, current_counter );
          ++counter;
        }
        printf( "thread finished" );
    } ) );
    printf( "\nthread AFTER reset" );
    thread_ptr->detach();
    printf( "\nthread detach" );
  }
  TestSleep( 10000 );
  return 0;
}
