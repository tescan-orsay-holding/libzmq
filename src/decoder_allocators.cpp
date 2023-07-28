/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "decoder_allocators.hpp"

#include "msg.hpp"
#include "reusable_memory_pool.hpp"

namespace zmq{

extern ReusableMemoryPool reusable_memory_pool;


shared_message_memory_allocator::shared_message_memory_allocator (
  std::size_t bufsize_) :
    _buf (NULL),
    _buf_size (0),
    _max_size (bufsize_),
    _msg_content (NULL),
    _max_counters ((_max_size + msg_t::max_vsm_size - 1) / msg_t::max_vsm_size) 
{
}



shared_message_memory_allocator::shared_message_memory_allocator (
  std::size_t bufsize_, std::size_t max_messages_) :
    _buf (NULL),
    _buf_size (0),
    _max_size (bufsize_),
    _msg_content (NULL),
    _max_counters (max_messages_)
{
}



shared_message_memory_allocator::shared_message_memory_allocator (
  std::size_t bufsize_,std::size_t max_messages_, bool use_memory_pool) :
    _buf (NULL),
    _buf_size (0),
    _max_size (bufsize_),
    _msg_content (NULL),
    _max_counters ((_max_size + msg_t::max_vsm_size - 1) / msg_t::max_vsm_size) 
{
    _use_memory_pool=use_memory_pool;
    if(use_memory_pool){
        _max_counters=max_messages_; 
    }
}



shared_message_memory_allocator::~shared_message_memory_allocator ()
{
    deallocate ();
}

unsigned char *shared_message_memory_allocator::allocate ()
{
    if (_buf) {
        // release reference count to couple lifetime to messages
        atomic_counter_t *c =
          reinterpret_cast<atomic_counter_t *> (_buf);

        // if refcnt drops to 0, there are no message using the buffer
        // because either all messages have been closed or only vsm-messages
        // were created
        if (c->sub (1)) {
            // buffer is still in use as message data. "Release" it and create a new one
            // release pointer because we are going to create a new buffer
            release ();
        }
    }

    // if buf != NULL it is not used by any message so we can re-use it for the next run
    if (!_buf) {
        std::cout<<"use memory pool"<<_use_memory_pool<<" counters:"<<_max_counters<<std::endl;
        if(_use_memory_pool){
            size_t buffer_size;
            _buf = reusable_memory_pool.allocate(buffer_size);
            _max_size=buffer_size-sizeof (atomic_counter_t)-_max_counters * sizeof (msg_t::content_t);
        }
        else{

            // allocate memory for reference counters together with reception buffer 
            std::size_t const allocationsize =
            _max_size + sizeof (atomic_counter_t)
            + _max_counters * sizeof (msg_t::content_t);

            _buf = static_cast<unsigned char *> (std::malloc (allocationsize));
        }
        alloc_assert (_buf);

        new (_buf) atomic_counter_t (1);
    
    } else {
        // release reference count to couple lifetime to messages
        atomic_counter_t *c =
          reinterpret_cast<atomic_counter_t *> (_buf);
        c->set (1);
    }

    _buf_size = _max_size;
    _msg_content = reinterpret_cast<msg_t::content_t *> (
      _buf + sizeof (atomic_counter_t) + _max_size);
    return _buf + sizeof (atomic_counter_t);
}

void shared_message_memory_allocator::deallocate ()
{
    atomic_counter_t *c = reinterpret_cast<atomic_counter_t *> (_buf);
    if (_buf && !c->sub (1)) {
        c->~atomic_counter_t ();
        if(reusable_memory_pool.enabled){
            reusable_memory_pool.deallocate(_buf);
        }
        else{
            std::free (_buf);
        }
    }
    clear ();
}

unsigned char *shared_message_memory_allocator::release ()
{
    unsigned char *b = _buf;
    clear ();
    return b;
}

void shared_message_memory_allocator::clear ()
{
    _buf = NULL;
    _buf_size = 0;
    _msg_content = NULL;
}

void shared_message_memory_allocator::inc_ref ()
{
    (reinterpret_cast<atomic_counter_t *> (_buf))->add (1);
}

void shared_message_memory_allocator::call_dec_ref (void *, void *hint_)
{
    
    zmq_assert (hint_);
    unsigned char *buf = static_cast<unsigned char *> (hint_);
    atomic_counter_t *c = reinterpret_cast<atomic_counter_t *> (buf);

    std::cout<<"call_dec_ref"<<static_cast<void *>(hint_)<<" count: "<<c->get() <<std::endl;

    if (!c->sub (1)) {
        c->~atomic_counter_t ();
        if(reusable_memory_pool.enabled){
            reusable_memory_pool.deallocate(buf);
        }
        else{
            std::free (buf);
        }
        
        buf = NULL;
    }
}


std::size_t shared_message_memory_allocator::size () const
{
    return _buf_size;
}

unsigned char *shared_message_memory_allocator::data ()
{
    return _buf + sizeof (atomic_counter_t);
}


void shared_message_memory_allocator::advance_content () {
    msg_counter++;
    assert(msg_counter<_max_counters); 
    _msg_content++;        
}

// void shared_message_memory_allocator::advance_content (size_t end_of_message) {
//     // if(_use_memory_pool){
//     //     //we disallow continuing in the same buffer
//     //     size_t previous_size=size();
//     //     unsigned char *previous_data=data();
//     //     unsigned char *previous_buf=release ();        
//     //     allocate ();
//     //     if(previous_size-end_of_message>0){
//     //         std::cout<<"copying: "<<previous_size-end_of_message<<" "<<previous_size<<" "<<end_of_message<<" |";
//     //         for(int i=0;i<previous_size-end_of_message;i++){
//     //             if((int) (previous_data+end_of_message)[i]<32){
//     //                 std::cout<<"X";
//     //             }
//     //             else{
//     //                 std::cout<<(previous_data+end_of_message)[i];
//     //             };
//     //         }
//     //         std::cout<<"|"<<std::endl;
//     //         memcpy(data(),previous_data+end_of_message,previous_size-end_of_message); //copy to new buffer;
//     //         resize(previous_size-end_of_message);
//     //     }
//     // }
//     // else{
//     msg_counter++;
//     assert(msg_counter<_max_counters);
//      _msg_content++;  
//     // }
        
// }


void shared_message_memory_allocator::resize (std::size_t new_size_) { 
    // if(_use_memory_pool){
    //     //we disallow continuing in the same buffer
    //     deallocate ();
    // }
    // else{
        _buf_size = new_size_;   
    // }


}

}
