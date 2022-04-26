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


static constexpr const size_t TestCapacity = 65500;

void TestSleep( size_t secs ) {
  using namespace std::chrono_literals;
  std::this_thread::sleep_for( secs * 1s );
}
void TestSleepMs( size_t secs ) {
  using namespace std::chrono_literals;
  std::this_thread::sleep_for( secs * 1ms );
}

struct TestListenerBase : public mapped_queue::IConsumer<int, int> {
  TestListenerBase(
      Event * stop_event_value,
      size_t thread_id_number,
      size_t thread_total_val,
      size_t final_val )
      :  thread_id_number_( thread_id_number ),
         stop_event_( stop_event_value ),
         thread_total_( thread_total_val ) {
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
  virtual void SetPoolAddress( mapped_queue::QueuePoolBase * addr ) {}
  virtual void SetTotalEnqueuedCounterAddress( std::atomic_int32_t * addr ) {}
  virtual std::atomic_uint32_t * GetCounterAddress() { return nullptr; }

 protected:
  static inline std::atomic_uint32_t result_counter_ = 0;
  static inline std::atomic_uint32_t final_counter_ = 0;
  size_t thread_total_;

 private:
  size_t thread_id_number_;
  Event * stop_event_;
};

std::atomic_size_t test12_elements_counter;

struct TestListener000 : public TestListenerBase {
  TestListener000( 
    Event * stop_event,
    size_t thread_id_number,
    size_t thread_total_val,
    size_t final_val ) : TestListenerBase( stop_event, thread_id_number, thread_total_val, final_val ) {}
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
template <size_t Capacity = TestCapacity> using PoolWithMutexes = mapped_queue::QueuePool< 
    TestKey, 
    TestValue, 
    Capacity, 
    TestTraits< TestKey, TestValue, Capacity>
> ;

template <size_t Capacity = TestCapacity> using PoolQueueMutex = mapped_queue::QueuePool< 
    TestKey, 
    TestValue, 
    Capacity, 
    TestTraitsQueueMutex< TestKey, TestValue, Capacity>
> ;

template <size_t Capacity = TestCapacity> using PoolQueueLockfree = mapped_queue::QueuePool< 
    TestKey, 
    TestValue, 
    Capacity, 
    TestTraitsQueueLockfree< TestKey, TestValue, Capacity>
> ;

template <size_t Capacity = TestCapacity> using PoolMapMutex = mapped_queue::QueuePool< 
    TestKey, 
    TestValue, 
    Capacity, 
    TestTraitsMapMutex< TestKey, TestValue, Capacity>
> ;

template <typename ListenerTn, typename PoolTn, typename CallbackTn>
void CreatePool( 
    PoolTn & pool,
    Event * stop_event,
    std::atomic_int32_t &counter,
    CallbackTn && callback, 
    size_t threads_number,
    size_t dequeued_wait_limit,
    std::vector< std::unique_ptr< std::jthread > > & threads,
    std::vector<std::atomic_uint32_t *> & nel_of_queue  ) {
  using namespace std;

  nel_of_queue.resize( threads_number );
  for ( size_t i = 1; i <= threads_number; ++i ) {
    shared_ptr< mapped_queue::IConsumer<int,int> > listener = 
      make_shared<ListenerTn>( stop_event, i, threads_number, dequeued_wait_limit );
    ( (TestListenerBase*)listener.get() )->SetPoolAddress( &pool );
    ( (TestListenerBase*)listener.get() )->SetTotalEnqueuedCounterAddress( &counter );
    auto * ptr =  ( (TestListenerBase*)listener.get() )->GetCounterAddress();
    nel_of_queue[i-1] = ptr;
    //nel_of_queue[threads_number-1] = ( (TestListenerBase*)listener.get() )->GetCounterAddress();
    pool.Subscribe( i, listener );
  }

  threads.resize( threads_number );
  atomic_int32_t thread_counter = 0;
  for ( auto & thread_ptr : threads ) {
    ++thread_counter;
    size_t current_thread = thread_counter;
    thread_ptr.reset( new jthread( [&nel_of_queue, &pool, &counter, &callback, current_thread ](){
        callback( pool, counter, current_thread, nel_of_queue );
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
  std::vector<std::atomic_uint32_t *>  nel_of_queue;
  constexpr const size_t kPushNumber = 1000;
  atomic_int32_t counter = 1;
  Event stop_event;
  CreatePool<TestListener000>( 
    *pool,
    &stop_event,
    counter,
    [kPushNumber]( PoolTn & pool, atomic_int32_t& counter, size_t current_thread, std::vector<atomic_uint32_t *>& ) {
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
    threads,
    nel_of_queue  );
  //TestSleep( 120 );
  stop_event.Wait();
  pool->StopProcessing();
  finished.WaitForConsumersFinished();
  //printf("TEST FINISHED");
}

/*
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
      std::make_shared<TestListener000>( &stop_event, i, 2, 8 );
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
      std::make_shared<TestListener000>( &stop_event, i, 2, 8 );
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
*/

struct TestListener012 : public TestListenerBase {
  TestListener012(
    Event * stop_event,
    size_t thread_id_number,
    size_t thread_total_val,
    size_t final_val ) : TestListenerBase( stop_event, thread_id_number, thread_total_val, final_val ) {}
  virtual void ConsumeInternal( int id, const int& value ) {
    // ждём, пока пройдёт половина данных, на половине данных фризим каждый поток консумера, даём очереди возможность переполнится
    --number_elements_counter_;
    size_t nel_counter = number_elements_counter_.load();
    auto consumers_total = queue_->GetConsumersNumber();
    size_t cluster = (   ( final_counter_ / thread_total_ ) / 2   ) * thread_total_;
    int32_t nel_old = number_elements_counter_;
    auto tot_enq_counter = total_enqueued_counter_->load();
    printf("\nDequeue %d, %d res_count=%u cluster=%u, prod_thread_max=%u consumers=%u final=%u total_enqueued_counter=%d", id, value, result_counter_.load(), cluster, thread_total_, consumers_total, final_counter_.load(), tot_enq_counter );
    if (total_enqueued_counter_->load() > 2102) {
      int x = 0;
    }
    if (   tot_enq_counter >= cluster && tot_enq_counter < ( cluster + thread_total_ )   ) {
      //if ( number_elements_counter_ >= TestCapacity - 10 ) {
      int32_t nel_local, nel_diff;
      int x = 0;
      do {       
        TestSleepMs( 200 );
        nel_local = number_elements_counter_.load();
        nel_diff = nel_local - nel_old;
        printf( "\nnel_local=%d diff=%d", nel_local, nel_diff );
      } while ( nel_diff < TestCapacity );
      TestSleepMs( 200 );
    }
  }
  virtual void SetPoolAddress( mapped_queue::QueuePoolBase * addr ) override {
    queue_ = addr;
  }
  virtual std::atomic_uint32_t * GetCounterAddress() {
    return &number_elements_counter_;
  }
  virtual void SetTotalEnqueuedCounterAddress( std::atomic_int32_t * addr ) { total_enqueued_counter_ = addr; }
  virtual bool NeedFinish() { return result_counter_ >= final_counter_; }

protected:
  mapped_queue::QueuePoolBase * queue_;
  std::atomic_uint32_t number_elements_counter_ {0};
  std::atomic_int32_t * total_enqueued_counter_ {nullptr};
};


template <typename PoolTn>
void Test012( size_t capacity ) { // на половине пути останавливаем потребление, и создаём ситуацию переполнения. Смотрим, зависнет или нет
  // прим. так как QueueWithMutexTraits не имеет по факту Capacity, то там и зависания быть не должно
  TestListenerBase::ResetCounter();
  test12_elements_counter = 0;
  mapped_queue::SignConsumerFinished finished;
  std::unique_ptr< PoolTn >  pool  (   PoolTn::Create( &finished )   );
  constexpr const size_t kThreadSize = 4;
  using namespace std;
  vector< unique_ptr< jthread > > threads;
  std::vector<std::atomic_uint32_t *>  nel_of_queue;
  size_t kPushNumber = capacity * 2 + ( capacity/10 ) ;
  atomic_int32_t counter = 1;
  Event stop_event;
  CreatePool<TestListener012>( 
    *pool,
    &stop_event,
    counter,
    [kPushNumber]( PoolTn & pool, atomic_int32_t& counter, size_t current_thread, std::vector<atomic_uint32_t *>& nel_of_queue ) {
      //printf("\nthread reset");
      for ( size_t i = 0; i <= kPushNumber; ++i ) {
        //if ( i == kPushNumber/2 )    TestSleep(25);
        size_t selected_queue = current_thread;
        size_t current_counter = counter;
        //printf( "\nenqueue call (thr=%u) {queue=%u val=%u} %u", current_thread, selected_queue, current_counter, i );
        pool.Enqueue( selected_queue, current_counter );
        ++(*nel_of_queue[current_thread-1]);
        ++counter;
      }
      //printf( "thread finished" );    
    }, 
    kThreadSize,
      kThreadSize * (kPushNumber + 1),
      threads,
      nel_of_queue  );
  //TestSleep( 120 );
  stop_event.Wait();
  pool->StopProcessing();
  finished.WaitForConsumersFinished();
  //printf("TEST FINISHED");
}


TEST( QueuePool, Test012 ) { // на половине пути останавливаем потребление, и создаём ситуацию переполнения. Смотрим, зависнет или нет
                             // прим. так как QueueWithMutexTraits не имеет по факту Capacity, то там и зависания быть не должно
  static constexpr const size_t kCapacity = 500;
  Test012<PoolQueueLockfree<kCapacity> >( kCapacity );
  SUCCEED();
}
/*TEST(QueuePool, Test013) { // на половине пути останавливаем потребление, и создаём ситуацию переполнения. Смотрим, зависнет или нет
                             // прим. так как QueueWithMutexTraits не имеет по факту Capacity, то там и зависания быть не должно
  Test012<PoolQueueMutex<kCapacity> >( kCapacity );
  SUCCEED();
}
TEST( QueuePool, Test014 ) { // на половине пути останавливаем потребление, и создаём ситуацию переполнения. Смотрим, зависнет или нет
                             // прим. так как QueueWithMutexTraits не имеет по факту Capacity, то там и зависания быть не должно
  Test012<PoolMapMutex<kCapacity> >( kCapacity );
  SUCCEED();
}
TEST( QueuePool, Test015 ) { // на половине пути останавливаем потребление, и создаём ситуацию переполнения. Смотрим, зависнет или нет
                             // прим. так как QueueWithMutexTraits не имеет по факту Capacity, то там и зависания быть не должно
  Test012<PoolWithMutexes<kCapacity> >( kCapacity );
  SUCCEED();
}*/

int main(int argc, char* argv[])
{
  testing::InitGoogleTest();
  RUN_ALL_TESTS();
  return 0;
}
