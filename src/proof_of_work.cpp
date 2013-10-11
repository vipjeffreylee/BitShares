#include <bts/proof_of_work.hpp>
#include <fc/crypto/sha512.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/crypto/aes.hpp>
#include <fc/crypto/salsa20.hpp>
#include <fc/crypto/city.hpp>
#include <string.h>

#include <fc/io/raw.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <utility>
#include <fc/log/logger.hpp>

#include <unordered_map>
#define BUF_SIZE (512)
#define BLOCK_SIZE (32) // bytes

namespace bts  {



/**
 *  This proof-of-work is computationally difficult even for a single hash,
 *  but must be so to prevent optimizations to the required memory foot print.
 *
 *  The maximum level of parallelism achievable per GB of RAM is 128, and the highest
 *  end GPUs now have 4 GB of ram which means they could in theory support 512 
 *  parallel execution of this proof-of-work.     
 *
 *  On GPU's you only tend to get 1 instruction per 4 clock cycles in a single
 *  thread context.   Modern super-scalar CPU's can get more than 1 instruction
 *  per block and CityHash is specifically optomized to take advantage of this. 
 *  In addition to getting more done per-cycle, CPU's have close to 4x the clock
 *  frequency.  Furthemore, this algorithm leverages CPU AES acceleration which gives
 *  a 4-5x performance gain over software implementations.
 *
 *  Performance is also limited by CPU cache and memory bus speed.  In this case,
 *  the 8 MB of memory is randomly accessed which should prevent the GPUs from
 *  successfully caching values in their local memory forcing them to operate out
 *  of the much slower global memory.
 *
 *  Based upon these characteristics alone, I estimate that a CPU can execute the
 *  serial portions of this algorithm at least 16x faster than a GPU which means
 *  that an 8-core CPU should easily compete with a 128 core GPU. Fortunately,
 *  a 128 core GPU would require 1 GB of RAM.  Note also, that most GPUs have 
 *  less than 128 'real' cores that are able to handle non data-parallel 
 *  operations. 
 *
 *  Further more, GPU's are not well suited for branch misprediction and code
 *  must be optimized to avoid branches as much as possible. When data parallel
 *  code goes down different branches the other branches must stall waiting
 *  on the the other branches to complete.  This hash function has a variable
 *  runtime depending upon the inputs which means that the GPU will be idling
 *  a large number of its data parallel threads.
 *
 *  Lastly this algorithm takes advantage of a hardware instruction that is
 *  unlikely to be included in GPUs (Intel CRC32).  The lack of this hardware
 *  instruction alone is likely to give the CPU an order of magnitude advantage
 *  over the GPUs.
 *
 *  NOTE: this algorithm has not been throughly reviewed for crytpographic
 *  security and may change at any time prior to launch.
 */
pow_hash proof_of_work( const fc::sha256& seed, unsigned char* buffer )
{
//#if 0
   auto key = fc::sha256(seed);
   auto iv  = fc::city_hash128((char*)&seed,sizeof(seed));
   memset( buffer, 0, BUF_SIZE );

   uint64_t* read_pos  = (uint64_t*)(buffer);
   uint64_t* write_pos = (uint64_t*)(buffer+32);

   fc::aes_encoder enc;
   enc.init( key, iv );
   for( uint32_t i = 0; i < BUF_SIZE / (BLOCK_SIZE/2); ++i )
   {
      /*uint64_t wrote =*/ enc.encode( (char*)read_pos, BLOCK_SIZE, (char*)write_pos ); 
      read_pos  =  (uint64_t*)( buffer + (write_pos[0]) % ( BUF_SIZE - BLOCK_SIZE ) );
      if( write_pos[2] % 117 == 0 && i > 32 ) { i -= write_pos[3]%32; }
      write_pos =  (uint64_t*)( buffer + (write_pos[1]) % ( BUF_SIZE - BLOCK_SIZE ) );
   }
   auto midstate =  fc::city_hash_crc_256( (char*)buffer, BUF_SIZE ); 
//#endif
//   auto midstate =  fc::city_hash_crc_256( (char*)&seed, sizeof(seed) ); 
   return fc::ripemd160::hash((char*)&midstate, sizeof(midstate) );
}


struct birthday_entry
{
  birthday_entry():found(0){}
  uint64_t nonce[2];
  uint8_t found;
};

pow_hash validate_proof_of_work( const fc::sha256& in, uint64_t* nonces )
{
   unsigned char* buf = new unsigned char[BUF_SIZE];
   uint64_t target = 0;
   {
      fc::sha256::encoder enc;
      enc.write( (char*)&nonces[0], sizeof(uint64_t) );
      enc.write( (char*)&in, sizeof(in) );
      auto out = proof_of_work( enc.result(), buf );
      uint64_t* out_val = (uint64_t*)&out;
      target = (*out_val) >> 26;
   }
   {
      fc::sha256::encoder enc;
      enc.write( (char*)&nonces[1], sizeof(uint64_t) );
      enc.write( (char*)&in, sizeof(in) );
      auto out = proof_of_work( enc.result(), buf );
      uint64_t* out_val = (uint64_t*)&out;
      FC_ASSERT( ((*out_val) >> 26 ) == target );
   }
   {
      fc::sha256::encoder enc;
      enc.write( (char*)&nonces[2], sizeof(uint64_t) );
      enc.write( (char*)&in, sizeof(in) );
      auto out = proof_of_work( enc.result(), buf );
      uint64_t* out_val = (uint64_t*)&out;
      FC_ASSERT( ((*out_val) >> 26 ) == target );
   }

   fc::ripemd160::encoder enc;
   enc.write( (char*)&in, sizeof(in) );
   enc.write( (char*)nonces, 3*sizeof(uint64_t) );
   return enc.result();
}



pow_hash proof_of_work( const fc::sha256& in, uint64_t* nonces)
{
   unsigned char* buf = new unsigned char[BUF_SIZE];
   uint64_t found_matches[2];
   found_matches[0] = 0;
   found_matches[1] = 0;

   //std::vector<uint64_t> found_nonces;
   //found_nonces.resize( (2ll << 32) / 8 );
   std::unordered_map<uint64_t,uint64_t> found0;
   std::unordered_map<uint64_t,uint64_t> found1;
   //memset( found_nonces.data(), 0, sizeof(uint64_t) * found_nonces.size() );

   pow_hash out;
   int partial_found = 0;
   try {
     for( uint64_t n = 1; n < uint64_t(-1); ++n )
     {
         fc::sha256::encoder enc;
         enc.write( (char*)&n, sizeof(n) );
         enc.write( (char*)&in, sizeof(in) );

         out = proof_of_work( enc.result(), buf );
         uint64_t* out_val = (uint64_t*)&out;
         
         uint64_t target = (*out_val) >> 27;
         if(  found0.find(target) != found0.end()  )
         {
            if(  found1.find(target) != found1.end()  )
            {
               ilog( "found ${a} and ${b} and ${c} all share ${t}  ", ("a",found0[target])("b",found1[target])("c",n)("t", target) );
               nonces[0] = found0[target];
               nonces[1] = found1[target];
               nonces[2] = n;
               fc::ripemd160::encoder enc;
               enc.write( (char*)&in, sizeof(in) );
               enc.write( (char*)nonces, 3*sizeof(uint64_t) );
               return enc.result();
            }
            else
            {
               ++partial_found;
               found1[target] = n;
               ilog( ", ${b} , ${p} ,found ${a} and ${b} both share ${t}  ", ("p",partial_found)("a",found0[target])("b",n)("t", target) );
            }
         }
         else
         {
            found0[target] = n;
         }
     }
   } catch ( ... )
   {
    delete [] buf;
    throw;
   }
   delete[] buf;
   return out;
}




}  // namespace bts
