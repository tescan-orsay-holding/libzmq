/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_DECODER_HPP_INCLUDED__
#define __ZMQ_DECODER_HPP_INCLUDED__

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iostream>

#include "decoder_allocators.hpp"
#include "err.hpp"
#include "i_decoder.hpp"
#include "stdint.hpp"

namespace zmq
{
//  Helper base class for decoders that know the amount of data to read
//  in advance at any moment. Knowing the amount in advance is a property
//  of the protocol used. 0MQ framing protocol is based size-prefixed
//  paradigm, which qualifies it to be parsed by this class.
//  On the other hand, XML-based transports (like XMPP or SOAP) don't allow
//  for knowing the size of data to read in advance and should use different
//  decoding algorithms.
//
//  This class implements the state machine that parses the incoming buffer.
//  Derived class should implement individual state machine actions.
//
//  Buffer management is done by an allocator policy.
template <typename T, typename A = c_single_allocator>
class decoder_base_t : public i_decoder
{
  public:
    explicit decoder_base_t (const size_t buf_size_) :
        _next (NULL), _read_pos (NULL), _to_read (0), _allocator (buf_size_)
    {
        _buf = _allocator.allocate ();
    }
    explicit decoder_base_t (const size_t buf_size_, int max_messages_, const bool use_memory_pool) :
        _next (NULL), _read_pos (NULL), _to_read (0), _allocator (buf_size_,max_messages_,use_memory_pool)
    {
        _buf = _allocator.allocate ();
    }

    ~decoder_base_t () ZMQ_OVERRIDE { _allocator.deallocate (); }

    //  Returns a buffer to be filled with binary data.
    void get_buffer (unsigned char **data_, std::size_t *size_) ZMQ_FINAL
    {

        if(_old_to_process>0){
            //std::cout<<"get buffer with old data"<<std::endl;
            _buf = _allocator.data()+_old_to_process;
            *size_ = _allocator.size ()-_old_to_process;

            // std::cout<<"old: "<<_old_to_process<<" "<<static_cast<void *>(_allocator.data())<<std::endl;

            // std::cout<<"  |";
            // for(int i=0;i<_old_to_process;i++){
            //     if(_allocator.data()[i]<32){
            //         std::cout<<"_";
            //     }
            //     else{
            //         std::cout<<_allocator.data()[i];
            //     }
                
            // }
            // std::cout<<"|"<<std::endl;

        }
        else{
            //std::cout<<"get new buffer"<<std::endl;
            _buf = _allocator.allocate ();
            //  If we are expected to read large message, we'll opt for zero-
            //  copy, i.e. we'll ask caller to fill the data directly to the
            //  message. Note that subsequent read(s) are non-blocking, thus
            //  each single read reads at most SO_RCVBUF bytes at once not
            //  depending on how large is the chunk returned from here.
            //  As a consequence, large messages being received won't block
            //  other engines running in the same I/O thread for excessive
            //  amounts of time.
            if (_to_read >= _allocator.size ()) {
                //std::cout<<"filling in msg "<<_to_read<<"/"<<_allocator.size ()<<std::endl;
                *data_ = _read_pos;
                *size_ = _to_read;
                return;
            }
            *size_ = _allocator.size ();
        }

        *data_ = _buf;
        
        //std::cout<<"get_buffer "<<static_cast<void *>(*data_)<<" "<<*data_<<std::endl;
    }

    //  Processes the data in the buffer previously allocated using
    //  get_buffer function. size_ argument specifies number of bytes
    //  actually filled into the buffer. Function returns 1 when the
    //  whole message was decoded or 0 when more data is required.
    //  On error, -1 is returned and errno set accordingly.
    //  Number of bytes processed is returned in bytes_used_.
    int decode (const unsigned char *data_,
                std::size_t size_,
                std::size_t &bytes_used_) ZMQ_FINAL
    {
        bytes_used_ = 0;
        //std::cout<<"decoding:"<<size_<<" to read:"<<((size_t) _to_read)<<" "<<bytes_used_<<" "<< reinterpret_cast<const void *>(data_)<< " -> "<<reinterpret_cast<const void *>(_read_pos)<<std::endl;
            

        //first process previously copied data already in the buffer;
        bool old=false;
        if(_old_to_process>0) {
            old=true;
            //this can only happen if we are in the middle of incoming message

            //std::cout<<"processing old data"<<_old_to_process<<std::endl;
            //  Copy the data from buffer to the message.
            const size_t to_copy = _old_to_process;
            // Only copy when destination address is different from the
            // current address in the buffer.
            if (_read_pos != _old_pos) {
                //std::cout<<"!!!!COPYING OLD DATA!!!"<<reinterpret_cast<const void *>(_old_pos)<<"->"<<static_cast<void *>(_read_pos)<<" sz:"<<to_copy<<std::endl;
                memcpy (_read_pos, _old_pos, to_copy);
            }

            _read_pos += to_copy;
            _to_read -= to_copy; 

            _old_to_process-=to_copy;            
            _old_pos+=to_copy;
        }

        //  In case of zero-copy simply adjust the pointers, no copying
        //  is required. Also, run the state machine in case all the data
        //  were processed.
        if (data_ == _read_pos && !old) {
            zmq_assert (size_ <= _to_read);
            _read_pos += size_;
            _to_read -= size_;
            bytes_used_ = size_;

            while (!_to_read) {
                //std::cout<<"msg B"<<std::endl;
                const int rc =
                  (static_cast<T *> (this)->*_next) (data_ + bytes_used_);
                if (rc != 0)
                    return rc;
            }
            return 0;
        }

        while (bytes_used_ < size_) {
            //  Copy the data from buffer to the message.
            const size_t to_copy = std::min (_to_read, size_ - bytes_used_);
            // Only copy when destination address is different from the
            // current address in the buffer.
            if (_read_pos != data_ + bytes_used_) {
                // if(to_copy>30){
                //     std::cout<<"!!!!COPYING!!!"<<reinterpret_cast<const void *>(data_ + bytes_used_)<<"->"<<static_cast<void *>(_read_pos)<<" sz:"<<to_copy<<std::endl;
                // }
                
                memcpy (_read_pos, data_ + bytes_used_, to_copy);
            }


            _read_pos += to_copy;
            _to_read -= to_copy;
            bytes_used_ += to_copy;
            //  Try to get more space in the message to fill in.
            //  If none is available, return.
            while (_to_read == 0) {
                // pass current address in the buffer
                //std::cout<<"msg fill in"<<size_<<" "<< _to_read<<std::endl;
                //std::cout<<"msg C"<<std::endl;
                const int rc =
                  (static_cast<T *> (this)->*_next) (data_ + bytes_used_);

                //std::cout<<"step done "<<rc<<" "<<bytes_used_<<" "<<size_<<std::endl;
                if(rc==2){
                    
                    bytes_used_=size_;
                    return 0;
                }
                
                if (rc != 0)
                    return rc;
            }
        }

        return 0;
    }
    void resize_buffer (std::size_t new_size_)
    {
        //std::cout<<"resize "<<new_size_<<std::endl;
        _allocator.resize (new_size_+_old_to_process);
    }




  protected:
    //  Prototype of state machine action. Action should return false if
    //  it is unable to push the data to the system.
    typedef int (T::*step_t) (unsigned char const *);

    //  This function should be called from derived class to read data
    //  from the buffer and schedule next state machine action.
    void next_step (void *read_pos_, std::size_t to_read_, step_t next_)
    {
        //std::cout<<"NEXT STEP "<<to_read_<<" "<<next_<<" "<<read_pos_<<std::endl;
        _read_pos = static_cast<unsigned char *> (read_pos_);
        _to_read = to_read_;
        _next = next_;
    }

    A &get_allocator () { return _allocator; }

    //previous non-processed data yet (due to buffer copying)
    std::size_t _old_to_process=0;
    unsigned char *_old_pos;

  private:
    //  Next step. If set to NULL, it means that associated data stream
    //  is dead. Note that there can be still data in the process in such
    //  case.
    step_t _next;

    //  Where to store the read data.
    unsigned char *_read_pos;

    //  How much data to read before taking next step.
    std::size_t _to_read;




    //  The duffer for data to decode.
    A _allocator;
    unsigned char *_buf;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (decoder_base_t)
};
}

#endif
