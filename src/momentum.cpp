#include <bts/momentum.hpp>
#include <fc/thread/thread.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <fc/crypto/sha1.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/crypto/sha512.hpp>
#include <fc/crypto/aes.hpp>

#include <unordered_map>
#include <fc/reflect/variant.hpp>
#include <fc/time.hpp>
#include <algorithm>

//#include <bts/hashtable.hpp>
#include <fc/log/logger.hpp>

#define MAX_NONCE  (1<<26)

namespace bts 
{
    
   std::vector< std::pair<uint32_t,uint32_t> > momentum_search( pow_seed_type head )
   {
      std::unordered_map<uint64_t,uint32_t>  found;
      found.reserve( MAX_NONCE );
      std::vector< std::pair<uint32_t,uint32_t> > results;

      for( uint32_t i = 0; i < MAX_NONCE;  )
      {
          fc::sha512::encoder enc;
          enc.write( (char*)&i, sizeof(i) );
          enc.write( (char*)&head, sizeof(head) );

          auto result = enc.result();
        
          for( uint32_t x = 0; x < 8; ++x )
          {
              uint64_t birthday = result._hash[x] >> 14;
              uint32_t nonce = i+x;
              auto itr = found.find( birthday );
              if( itr != found.end() )
              {
                  results.push_back( std::make_pair( itr->second, nonce ) );
              }
              else
              {
                  found[birthday] = nonce;
              }
          }
          i += 8;
      }
      return results;
   }


   bool momentum_verify( pow_seed_type head, uint32_t a, uint32_t b )
   {
          if( a == b ) return false;
          if( a > MAX_NONCE ) return false;
          if( b > MAX_NONCE ) return false;

          uint32_t ia = (a / 8) * 8; 
          fc::sha512::encoder enca;
          enca.write( (char*)&ia, sizeof(ia) );
          enca.write( (char*)&head, sizeof(head) );
          auto ar = enca.result();

          uint32_t ib = (b / 8) * 8; 
          fc::sha512::encoder encb;
          encb.write( (char*)&ib, sizeof(ib) );
          encb.write( (char*)&head, sizeof(head) );
          auto br = encb.result();

          return (ar._hash[a%8]>>14) == (br._hash[b%8]>>14);
   }

}
