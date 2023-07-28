#include "precompiled.hpp"
#include "reusable_memory_pool.hpp"

namespace zmq
{
ReusableMemoryPool reusable_memory_pool{}; // we need to define it as singleton, because of the dereferencing process;


}
