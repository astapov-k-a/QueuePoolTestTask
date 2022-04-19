#ifndef    QUEUEPOOL_H_PROTECTOR_UM2CSA2J0K3LX5SVVQ4YV8BW8P5ZVBHKLMNMYJIGKGPAON
#define    QUEUEPOOL_H_PROTECTOR_UM2CSA2J0K3LX5SVVQ4YV8BW8P5ZVBHKLMNMYJIGKGPAON

/**
** @file QueuePool.h
** @author Astapov K.A.
** @remark ЗАДАЧА:
**         Нужно реализовать неопределённое количество очередей, доступ к которым осуществляется
**         по произвольному ключу. Очередь создаётся через подписку, при этом каждой очереди передаётся
**         класс-обработчик/подписчик/IConsumer
**         При этом число потоков-производителей соответствует числу очередей, а поток-потребитель один
**         (в дальнейшем, возможно, сделаю произвольное число потоков-потребителей)
**         Ограничений на имплементацию никаких нет.
**
**         РЕШЕНИЕ:
** 
**         1) Бессмысленно делать N очередей, если поток-потребитель(consumer) только один. 
**         И даже если бы было несколько потребителей, всё равно разумнее одна очередь lock-free
**         Так как ограничений на имплементацию нет, то несколько очередей эмулируется одной очередью из
**         элементов ключ-значение. Это позволяет реализовать большинство требований задания, например,
**         уменьшение зависимости от числа очередей.
**
**         2) Помимо этого, мною было решено уменьшить число выделений памяти, т.к. выделение памяти
**            может вызывать глобальную блокировку ( что уменьшает производительность ) и может
**            само по себе быть дорогим. Для этого я подбирал lock-free очередь с заранее выделенным
**            фиксированным размером
**    
**         3) Так как использование сторонних lock-free очередей ограничено только бустом,
**         а бустовое решение имеет максимальный размер фиксированной очереди в 65535 элементов, что не так много,
**         я дополнительно реализовал ожидание потока в случае переполнения (имплементация на условной переменной)
**
**         4) Так же мною было реализовано ожидание в случае опустошения очереди  (имплементация на условной переменной)
**
**         5) Для того, чтобы увеличить эффективность параллельного кода (и уменьшить вредное влияние закона Амдала)
**            я поставил целью уменьшить число блокировок, или уменьшить число потоков, засыпающих на блокировке. Так как
**            очередь выбрана lock-free, единственный источник блокировок - это поиск в map'е указателя на listener по ключу.
**            Чтобы уменьшить их вредный эффект, я написал самописный map - SafeUnorderedMap, в котором на каждый bucket
**            своя блокировка. По умолчанию bucket'ов 256, поэтому вероятность того, что на два и более потока будут 
**            читать/писать из одного бакета. Так же я решил использовать read-write lock, потому что изменение listener'ов
**            должно быть редким, а чтение частым. Как реализацию read-write lock я выбрал std::shared_mutex
**
**         6) Так же можно отключить очередь lock-free, заменив на std::queue + std::mutex, задав QUEUE_WITH_MUTEX 1
**            и заменить SafeUnorderedMap на std::map + std::mutex, задав SAFE_MAP 0
**/
#define PROCESS_ERROR(Text) { printf("ERROR %s", Text); }

#include "QueuePoolIConsumer.h"
#include <memory>
#include <mutex>
#include <thread>
#include <optional>
#include <stop_token>
#include "QueuePoolFixedSizeLockfreeQueueTraits.h"
#include "QueuePoolSafeMapTraits.h"


//#define PRDEBUG printf
#define PRDEBUG 



namespace mapped_queue {

template < 
    typename KeyTn, 
    typename ValueTn, 
    size_t CapacityTn,
    typename QueueTraitsTn = FixedSizeLockfreeQueueTraits<KeyTn, ValueTn, CapacityTn>,
    typename MapTraitsTn   = SafeMapTraits<KeyTn, ValueTn, CapacityTn>  >
struct DefaultTraits {
  constexpr static const size_t Capacity = CapacityTn;

  typedef KeyTn Key;
  typedef ValueTn Value;
  typedef std::shared_ptr< IConsumer<Key, Value> > Listener;
  typedef QueueTraitsTn QueueTraits;
  typedef MapTraitsTn MapTraits;

  typedef QueueTraits::QueueNode QueueNode;
  typedef QueueTraits::Queue Queue;
  typedef MapTraits::Map Map;


  static bool Enqueue( Queue& queue, const Key& key_value, const Value& the_value ) {
    return QueueTraits::Enqueue( queue, key_value, the_value );
  }
  static bool Dequeue( Queue& queue, QueueNode & result ) {
    return QueueTraits::Dequeue( queue,result );
  }
  static void AddToMap(
      Map& map,
      const Key& key_value,
      const Listener& listener_value ) {
    MapTraits::AddToMap( map, key_value, listener_value );
  }
  static void ExcludeFromMap( const Map& map, const Key& key_value ) {
    MapTraits::ExcludeFromMap( map, key_value );
  }
  static Listener GetFromMap( Map& map, const Key& key_value ) {
    return MapTraits::GetFromMap( map, key_value );
  }
};

class QueuePoolBase {
 public:
  void StopProcessing() {
    stop_command_.request_stop();
  }
  std::stop_token GetStopToken() {
    return stop_command_.get_token();
  }

 private:
  std::stop_source stop_command_;
};

template <
  typename KeyTn,
  typename ValueTn,
  size_t TotalCapacity,
  typename TraitsTn// = DefaultTraits<KeyTn, ValueTn, TotalCapacity>
> class QueuePool : public QueuePoolBase {
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
        ConsumerThreadFunctionStatic( ret->GetStopToken(), ret);
        }) );
      ret->thrd_->detach();
    }
    printf( "\nCreate finished" );
    return ret;
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
        //printf("\nE!!!");
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
  void StartDestructEvent() {
    consume_condvar_.notify_all();
  }
  void StartEndCapacityEvent() {
    enqueue_condvar_.notify_one();
  }
  void Dequeue( std::stop_token stop_token ) {
    PRDEBUG( "\nDequeue started" );
    thread_local QueueNode node_local; // thread_local нужен, чтобы убрать избыточный вызов конструктора по умолчанию
    QueueNode & node = * (&node_local);
    bool has_value = Traits::Dequeue( queue_, node );
    if ( !has_value ) {
      PRDEBUG("\nDequeue freeze");
      std::unique_lock<std::mutex> locker( condit_consume_mutex_ ); {
        auto & queue_local = queue_;
        consume_condvar_.wait( locker, [&node, &has_value, &queue_local, &stop_token]() {
          has_value = Traits::Dequeue( queue_local, node );
          return has_value || stop_token.stop_requested(); // false если надо продолжить ожидание
        } );
      }
    }
    if ( stop_token.stop_requested() ) return;
    PRDEBUG("\nDequeue unfreeze value = %u %u", node. key, node.value );
    auto listener = Traits::GetFromMap( map_, node.key );
    PRDEBUG("\nDequeue unfreeze listener");
    if ( (bool)listener ) {
      auto& value = node.value;
      PRDEBUG("\nDequeue start consume");
      listener->Consume( node.key, value );
      StartEndCapacityEvent();
    } else {
      PROCESS_ERROR("Unknown queue");
    }
  }
  void ConsumerThreadFunction( std::stop_token stop_token ) {
    size_t debug_counter = 0;
    while (1) {
      if ( stop_token.stop_requested() ) {
        StartDestructEvent();
        return;
      }
      Dequeue( stop_token );
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
  std::mutex condit_consume_mutex_;
  std::condition_variable consume_condvar_;
  std::mutex condit_enqueue_mutex_;
  std::condition_variable enqueue_condvar_;
};

} // namespace mapped_queue


#endif  // QUEUEPOOL_H_PROTECTOR_UM2CSA2J0K3LX5SVVQ4YV8BW8P5ZVBHKLMNMYJIGKGPAON
