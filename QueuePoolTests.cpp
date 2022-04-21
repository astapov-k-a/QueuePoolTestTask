#include "QueuePool.h"
#include "QueuePoolQueueWithMutexTraits.h"
#include "QueuePoolFixedSizeLockfreeQueueTraits.h"
#include "QueuePoolMapWithMutexTraits.h"
#include "QueuePoolSafeMapTraits.h"
#include <googletest/gtest.h>

#if __unix__
#   include <unistd.h>
#else
#   include <windows.h>
#endif


#define QUEUE_WITH_MUTEX 0
#define SAFE_MAP 1



void TestSleep( size_t secs ) {
  using namespace std::chrono_literals;
  std::this_thread::sleep_for( secs * 1s );
}

struct TestListenerBase : public mapped_queue::IConsumer<int, int> {
  TestListenerBase(
      Event * stop_event_value,
      size_t thread_id_number,
      size_t final_val )
      :  thread_id_number_( thread_id_number ),
         stop_event_( stop_event_value ) {
    final_counter_ = final_val;
  }
  static void ResetCounter() { result_counter_.store( 0 ); }

  virtual void Consume( int id, const int& value ) override
  {
    assert( id == thread_id_number_ );
    //printf( "\nconsume %d, %d %u", id, value, result_counter_.load() );
    ConsumeInternal( id, value );
    ++result_counter_;
    if ( NeedFinish() ) stop_event_->NotifyAll();
  }
  virtual void ConsumeInternal( int id, const int& value ) {}
  virtual bool NeedFinish() { return 0; }

 protected:
  static inline std::atomic_uint32_t result_counter_ = 0;
  static inline std::atomic_uint32_t final_counter_ = 0;

 private:
  size_t thread_id_number_;
  Event * stop_event_;
};

struct TestListener000 : public TestListenerBase {
  TestListener000( 
    Event * stop_event,
    size_t thread_id_number,
    size_t final_val ) : TestListenerBase( stop_event, thread_id_number, final_val ) {}
  virtual bool NeedFinish() { return result_counter_ >= final_counter_; }
};



template <typename Key, typename Value, size_t Capacity> using TestTraits = mapped_queue::DefaultTraits<
  Key,
  Value,
  Capacity,
  mapped_queue::QueueWithMutexTraits<Key, Value, Capacity>,
  mapped_queue::MapWithMutexTraits<Key, Value, Capacity> >;

template <typename Key, typename Value, size_t Capacity> using TestTraitsQueueMutex = mapped_queue::DefaultTraits<
  Key,
  Value,
  Capacity,
  mapped_queue::QueueWithMutexTraits<Key, Value, Capacity>,
  typename mapped_queue::SafeMapTraits<Key, Value, Capacity> >;

template <typename Key, typename Value, size_t Capacity> using TestTraitsQueueLockfree = mapped_queue::DefaultTraits<
    Key,
    Value,
    Capacity,
    typename mapped_queue::FixedSizeLockfreeQueueTraits<Key, Value, Capacity>,
    typename mapped_queue::SafeMapTraits<Key, Value, Capacity> >;

template <typename Key, typename Value, size_t Capacity> using TestTraitsMapMutex = mapped_queue::DefaultTraits<
    Key,
    Value,
    Capacity,
    typename mapped_queue::FixedSizeLockfreeQueueTraits<Key, Value, Capacity>,
    mapped_queue::MapWithMutexTraits<Key, Value, Capacity> >;

typedef int TestKey;
typedef int TestValue;
static constexpr const size_t TestCapacity = 65500;
typedef mapped_queue::QueuePool< 
    TestKey, 
    TestValue, 
    TestCapacity, 
    typename TestTraits< TestKey, TestValue, TestCapacity>
> PoolWithMutexes;

typedef mapped_queue::QueuePool< 
    TestKey, 
    TestValue, 
    TestCapacity, 
    typename TestTraitsQueueMutex< TestKey, TestValue, TestCapacity>
> PoolQueueMutex;

typedef mapped_queue::QueuePool< 
    TestKey, 
    TestValue, 
    TestCapacity, 
    typename TestTraitsQueueLockfree< TestKey, TestValue, TestCapacity>
> PoolQueueLockfree;

typedef mapped_queue::QueuePool< 
    TestKey, 
    TestValue, 
    TestCapacity, 
    typename TestTraitsMapMutex< TestKey, TestValue, TestCapacity>
> PoolMapMutex;

template <typename ListenerTn, typename PoolTn, typename CallbackTn>
void CreatePool( 
    PoolTn & pool,
    Event * stop_event,
    std::atomic_int32_t &counter,
    CallbackTn && callback, 
    size_t threads_number,
    size_t dequeued_wait_limit,
    std::vector< std::unique_ptr< std::jthread > > & threads  ) {

  for ( size_t i = 1; i <= threads_number; ++i ) {
    std::shared_ptr< mapped_queue::IConsumer<int,int> > listener = 
      std::make_shared<ListenerTn>( stop_event, i, dequeued_wait_limit );
    pool.Subscribe( i, listener );
  }

  using namespace std;
  threads.resize( threads_number );
  atomic_int32_t thread_counter = 0;
  for ( auto & thread_ptr : threads ) {
    ++thread_counter;
    size_t current_thread = thread_counter;
    thread_ptr.reset( new jthread( [&pool, &counter, &callback, current_thread](){
        callback( pool, counter, current_thread );
      } ) );
    //printf( "\nthread AFTER reset" );
    thread_ptr->detach();
    //printf( "\nthread detach" );
  }
}

template <typename PoolTn>
void Test000( ) {
  TestListenerBase::ResetCounter();
  mapped_queue::SignConsumerFinished finished;
  std::unique_ptr< PoolTn >  pool  (   PoolTn::Create( &finished )   );
  constexpr const size_t kThreadSize = 4;
  using namespace std;
  vector< unique_ptr< jthread > > threads;
  constexpr const size_t kPushNumber = 1000;
  atomic_int32_t counter = 1;
  Event stop_event;
  CreatePool<TestListener000>( 
    *pool,
    &stop_event,
    counter,
    [kPushNumber]( PoolTn & pool, atomic_int32_t& counter, size_t current_thread ) {
      //printf("\nthread reset");
      for ( size_t i = 0; i <= kPushNumber; ++i ) {
        //if ( i == kPushNumber/2 )    TestSleep(25);
        size_t selected_queue = current_thread;
        size_t current_counter = counter;
        //printf( "\nenqueue call (thr=%u) {queue=%u val=%u} %u", current_thread, selected_queue, current_counter, i );
        pool.Enqueue( selected_queue, current_counter );
        ++counter;
      }
      //printf( "thread finished" );    
    }, 
    kThreadSize,
    kThreadSize * (kPushNumber + 1),
    threads );
  //TestSleep( 120 );
  stop_event.Wait();
  pool->StopProcessing();
  finished.WaitForConsumersFinished();
  //printf("TEST FINISHED");
}


TEST( QueuePool, Test000 ) { // просто запуск цикла записать в N потоков - потребить из очереди. Если не зависло, то всё ок
  Test000< PoolQueueLockfree >();  
  SUCCEED();
}
TEST( QueuePool, Test001 ) { // просто запуск цикла записать в N потоков - потребить из очереди. Если не зависло, то всё ок
  Test000< PoolQueueMutex >();  
  SUCCEED();
}
TEST( QueuePool, Test002 ) { // просто запуск цикла записать в N потоков - потребить из очереди. Если не зависло, то всё ок
  Test000< PoolMapMutex >();  
  SUCCEED();
}
TEST( QueuePool, Test003 ) { // просто запуск цикла записать в N потоков - потребить из очереди. Если не зависло, то всё ок
  Test000< PoolWithMutexes >();  
  SUCCEED();
}

template <typename PoolTn> void Test004() {

  mapped_queue::SignConsumerFinished finished;
  std::unique_ptr< PoolTn > pool (   PoolTn::Create( &finished )   );

  pool->Enqueue( 1, 1 );
  pool->Enqueue( 1, 2 );
  pool->Enqueue( 1, 3 );
  pool->Enqueue( 1, 4 );
  pool->Enqueue( 2, 1 );
  pool->Enqueue( 2, 2 );
  pool->Enqueue( 2, 3 );
  pool->Enqueue( 2, 4 );
  Event stop_event;
  for ( size_t i = 1; i <= 2; ++i ) {
    std::shared_ptr< mapped_queue::IConsumer<int,int> > listener = 
      std::make_shared<TestListener000>( &stop_event, i, 8 );
    pool->Subscribe( i, listener );
  }

  stop_event.Wait();
  TestSleep( 1 );
  pool->StopProcessing();
  finished.WaitForConsumersFinished();
}
TEST( QueuePool, Test004 ) { // прерываем с пустой очередью, смотрим, зависнет, или нет
  Test004<PoolQueueLockfree>();
  SUCCEED();
}
TEST( QueuePool, Test005 ) { // прерываем с пустой очередью, смотрим, зависнет, или нет
  Test004<PoolQueueMutex>();
  SUCCEED();
}
TEST( QueuePool, Test006 ) { // прерываем с пустой очередью, смотрим, зависнет, или нет
  Test004<PoolMapMutex>();
  SUCCEED();
}
TEST( QueuePool, Test007 ) { // прерываем с пустой очередью, смотрим, зависнет, или нет
  Test004<PoolWithMutexes>();
  SUCCEED();
}

template <typename PoolTn> void Test008() {
  mapped_queue::SignConsumerFinished finished;
  std::unique_ptr< PoolTn > pool (   PoolTn::Create( &finished )   );

  pool->Enqueue( 1, 1 );
  pool->Enqueue( 1, 2 );
  pool->Enqueue( 1, 3 );
  pool->Enqueue( 1, 4 );
  pool->Enqueue( 2, 1 );
  pool->Enqueue( 2, 2 );
  pool->Enqueue( 2, 3 );
  pool->Enqueue( 2, 4 );
  Event stop_event;
  for ( size_t i = 1; i <= 2; ++i ) {
    std::shared_ptr< mapped_queue::IConsumer<int,int> > listener = 
      std::make_shared<TestListener000>( &stop_event, i, 8 );
    pool->Subscribe( i, listener );
  }

  stop_event.Wait();
  TestSleep( 1 );
  pool.reset();
  finished.WaitForConsumersFinished();
}

TEST( QueuePool, Test008 ) { // прерываем с пустой очередью, смотрим, зависнет, или нет
  Test008<PoolQueueLockfree>();
  SUCCEED();
}
TEST( QueuePool, Test009 ) { // прерываем с пустой очередью, смотрим, зависнет, или нет
  Test008<PoolQueueMutex>();
  SUCCEED();
}
TEST( QueuePool, Test010 ) { // прерываем с пустой очередью, смотрим, зависнет, или нет
  Test008<PoolMapMutex>();
  SUCCEED();
}
TEST( QueuePool, Test011 ) { // прерываем с пустой очередью, смотрим, зависнет, или нет
  Test008<PoolWithMutexes>();
  SUCCEED();
}

int main(int argc, char* argv[])
{
  testing::InitGoogleTest();
  RUN_ALL_TESTS();
  return 0;
}
