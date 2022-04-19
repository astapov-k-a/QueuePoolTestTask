#ifndef    QUEUEPOOL_ICONSUMER_H_PROTECTOR_S70AGWH5O8LRCV93TZREMTZA7T5S06L8WJUG
#define    QUEUEPOOL_ICONSUMER_H_PROTECTOR_S70AGWH5O8LRCV93TZREMTZA7T5S06L8WJUG

/**
 ** @file QueuePoolIConsumer.h
 ** @author Astapov K.A.
 **/

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

} // namespace mapped_queue


#endif  // QUEUEPOOL_ICONSUMER_H_PROTECTOR_S70AGWH5O8LRCV93TZREMTZA7T5S06L8WJUG
