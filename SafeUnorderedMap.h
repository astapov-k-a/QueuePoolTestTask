#ifndef    SAFEUNORDEREDMAP_H_PROTECTOR_VZ5HUF7RU4H03VRJ0UHX2NA014MPRDL22AXVO7L
#define    SAFEUNORDEREDMAP_H_PROTECTOR_VZ5HUF7RU4H03VRJ0UHX2NA014MPRDL22AXVO7L

#pragma once
#include <functional>
#include <mutex>
#include <map>
#include <memory>
#include <utility>


template <
    typename Key,
    typename Bucket,
    size_t NumberOfBuckets = 256,
    typename Hash = std::hash<Key> >
class HashMap {
 public:
  Bucket const& Get(const Key& key_value) const {
      return const_cast<Bucket const&>(UnconstThis()->Get(key_value));
  }
  Bucket& Get(const Key& key_value) {
      Hash hasher;
      return buckets_[hasher(key_value) % NumberOfBuckets];
  }

 protected:
   HashMap* UnconstThis() const { return const_cast<HashMap*>(this); }

 private:
  std::array< Bucket, NumberOfBuckets > buckets_;
};

template <typename Value, int Tolerance, typename Enable = void>
struct ThriftStorage {  
  ThriftStorage( Value * value_ptr ) : value_( value_ptr ) {
  }
  Value const* Get() const { return value_; }
  Value* Get() { return value_; }
  constexpr static const bool need_delete = true;

  private:
  Value* value_;
};

template <typename Value, int Tolerance>
    struct ThriftStorage<
    Value,
    Tolerance,
    typename std::enable_if< sizeof(Value) <= sizeof(void*) * Tolerance >::type
> {
  ThriftStorage( const Value & the_value ) : value_( the_value ) {
  }
  Value const* Get() const { return &value_; }
  Value* Get() { return &value_; }
  constexpr static const bool need_delete = false;

 private:
  Value value_;
};

template <
    typename Key,
    typename Value,
    typename Mutex = std::mutex,
    size_t NumberOfBuckets = 256,
    typename Hash = std::hash<Key>,
    typename Allocator = std::allocator<Value> 
>
class SafeUnorderedMap {
    typedef ThriftStorage< Value, 3> Element;
    typedef std::pair<const Key, Element> MapPair;

    typedef typename  std::allocator_traits<Allocator>::template rebind_alloc< MapPair > MapAllocator;
  struct Reference {
    typedef std::unique_lock<Mutex> Locker;
    Reference(Locker&& locker_val, Value* value_ptr)
        : locker(std::move(locker_val)),
        value(value_ptr) {
    }
    operator Value* () { return value; }
    operator const Value* () const { return value; }

    Value* value;
    Locker locker;
  };

  struct Bucket {
    typedef std::map<Key, Element, std::less<Key>, MapAllocator > InternalStorage;

    ~Bucket() {
        if constexpr ( Element::need_delete ) {
            Allocator alloc;
            for (auto& elem : data) {
                alloc.deallocate(elem.second.Get(), 1);
            }
        }
    }
    Reference Get( const Key& key ) {
        std::unique_lock<Mutex> locker( mutex );
        auto found = data.found( key );
        return Reference(
            std::move( locker ),
            ( found != data.end() ) ? found->Get() : nullptr  );
    }
    void Set( const Key& key, const Value& value ) {
        std::lock_guard<Mutex> locker( mutex );
        if constexpr (Element::need_delete) {
          Allocator alloc;
          data.emplace(    key, Element(   alloc.allocate( value )   )    );
        } else {
          data.emplace(  key, Element( value )  );
        }
    }
    void Erase( const Key& key ) {
        std::lock_guard<Mutex> locker( mutex );
        auto found = data.found( key );
        if ( found != data.end() ) {
            MapAllocator alloc;
            if constexpr (Element::need_delete) {
              alloc.deallocate( found->Get(), 1 );
            }
            data.erase( key );
        }
    }
    Reference GetAndCreate( const Key& key ) {
      std::unique_lock<Mutex> locker( mutex );
      auto found = data.find( key );
      if ( found == data.end() ) {
        found = data.emplace( key, Value() ).first;
      }
      Value * dd = found->second.Get();
      return Reference(
        std::move( locker ),
        ( found != data.end() ) ? found->second.Get() : nullptr  );
    }

  private:
    InternalStorage data;
    Mutex mutex;
  };

 public:
  typedef HashMap< Key, Bucket, NumberOfBuckets, Hash > HashStorage;

  Reference Get( const Key& key ) {
    auto & bucket = hash_map.Get( key );
    return bucket.Get( key );
  }
  void Set( const Key& key, const Value& value ) {
    auto & bucket = hash_map.Get( key );
    return bucket.Set( key, value );
  }
  void Erase( const Key& key ) {
    auto& bucket = hash_map.Get( key );
    return bucket.Erase( key );    
  }
  Reference GetAndCreate( const Key& key ) {
    auto& bucket = hash_map.Get( key );
    return bucket.GetAndCreate( key );
  }

 private:
  HashStorage hash_map;
};
#endif  // SAFEUNORDEREDMAP_H_PROTECTOR_VZ5HUF7RU4H03VRJ0UHX2NA014MPRDL22AXVO7L
