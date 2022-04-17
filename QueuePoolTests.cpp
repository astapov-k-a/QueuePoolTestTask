#include "QueuePool.h"


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
