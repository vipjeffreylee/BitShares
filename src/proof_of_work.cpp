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



pow_hash proof_of_work( const fc::sha256& seed )
{
   auto midstate =  fc::city_hash_crc_256( (char*)&seed, sizeof(seed) ); 
   return fc::ripemd160::hash((char*)&midstate, sizeof(midstate) );
}





}  // namespace bts
