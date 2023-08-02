#ifndef __ZMQ_REUSABLE_MEMORY_POOL_HPP_INCLUDED__
#define __ZMQ_REUSABLE_MEMORY_POOL_HPP_INCLUDED__

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>
#include <iostream>


namespace zmq
{
class ReusableMemoryPool{

    size_t buffer_size = 350'000; // this can be changed to while still presering previous items

    std::unordered_map<size_t,std::vector<unsigned char*>> buffers;
    std::unordered_map<size_t,std::vector<unsigned int>> empty_slots;
    std::unordered_map<size_t,std::unordered_map<unsigned char *,unsigned int>> buff_to_index_map;    
    

    std::mutex access_mutex;
    
public:
    bool enabled=true; //we need it for replace chek if to replace free function

    size_t get_buffer_size(){
        return buffer_size;
    }

    void set_buffer_size(size_t value,size_t expected_item_count){ 
        access_mutex.lock();
        buffer_size=value;
        if(buffers.find(value)==buffers.end()){

            std::vector<unsigned char*> vector;
            vector.reserve(expected_item_count);
            buffers.insert({value, vector});

            std::vector<unsigned int> vector2;
            vector.reserve(expected_item_count);
            empty_slots.insert({value, vector2});

            std::unordered_map<unsigned char *,unsigned int> map;
            buff_to_index_map.insert({value,map});
        } 
        //clean up space from other buffers;
        for (auto& it0 : buff_to_index_map){
            auto key=it0.first;
            if(key!=value){
                for(const auto &it:empty_slots[key]){
                    if(buffers[key][it]!=nullptr){
                        buff_to_index_map[key].erase(buffers[key][it]);
                        free(buffers[key][it]);
                        buffers[key][it]=nullptr;
                    }                    
                }
            }
        }
        access_mutex.unlock();
    }

    //provide memory with fixed buffer size;
    unsigned char * allocate(size_t &size){    
        access_mutex.lock(); 
        size=buffer_size; 
        if(empty_slots[size].empty()){
            unsigned char * buffer=(unsigned char *) malloc(size);
            if(!buffer){
                throw("fatal error out of memory");
            }
            buffers[size].push_back(buffer);
            buff_to_index_map[size].insert({buffer,buffers[size].size()-1});
            
            std::cout<<"allocate "<<buffers[size].size()<<" "<<static_cast<void *>(buffer)<<std::endl;
            access_mutex.unlock();
            return buffer;
        }
        else{
            const unsigned int index=empty_slots[size].back();
            empty_slots[size].pop_back();
            if(buffers[size][index]==nullptr){
                //this can happen if we reuse the location from different buffer_size;
                unsigned char * buffer=(unsigned char *) malloc(size);
                if(!buffer){
                    throw("fatal error out of memory");
                }
                buffers[size][index]=buffer;
                buff_to_index_map[size].insert({buffer,index});
            }
            //std::cout<<"reallocate "<<buffers[size].size()<<" "<<static_cast<void *>(buffers[size][index])<<std::endl;
            access_mutex.unlock();            
            return buffers[size][index];
        }
        
    }


    bool deallocate(unsigned char* buffer){
        access_mutex.lock();   
        //std::cout<<"deallocate "<<static_cast<void *>(buffer)<<std::endl;     
        bool found=false;
        for (const auto& it0 : buff_to_index_map){
            auto it=it0.second.find(buffer);
            if(it!=it0.second.end()){
                empty_slots[it0.first].push_back(it->second);
                if(it0.first!=buffer_size){
                    //clean up space from the pool of different buffer_size
                    buff_to_index_map[it0.first].erase(buffers[it0.first][it->second]);
                    free(buffer);
                    buffers[it0.first][it->second]=nullptr;
                }
                found=true;
                break;
            }

        }
        if(!found){
            free(buffer); // we dont own the object back to system free
            access_mutex.unlock();
            return false;
        }
        access_mutex.unlock();
        return true;
    }




    ~ReusableMemoryPool (){
        reset();
    }

    //use with care, all of the previously allocated items will become deallocated !!!!!
    void reset(){
        access_mutex.lock();
        for (const auto& it : buffers){    
            auto key=it.first;
            for(unsigned int i=0;i<buffers[key].size();i++){
                free(buffers[key][i]);
            }
            buffers[key].clear();
            empty_slots[key].clear();
            buff_to_index_map[key].clear();
        }
        buffers.clear();
        empty_slots.clear();
        buff_to_index_map.clear();
        access_mutex.unlock();
    }

};



}
#endif