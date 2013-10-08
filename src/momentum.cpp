#include <bts/momentum.hpp>
#include <fc/thread/thread.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/crypto/sha512.hpp>
#include <fc/crypto/aes.hpp>

#include <unordered_map>

namespace momentum 
{
    
   uint64_t pow_birthday_hash( const pow_seed_type& s, uint64_t nonce )
   {
        uint64_t in_buffer[32];
        memset( (char*)in_buffer, 0, sizeof(in_buffer) );
        in_buffer[0] = nonce;
        memcpy( (char*)&in_buffer[1], (char*)&s, sizeof(s) );

        uint64_t out_buffer[sizeof(in_buffer)/sizeof(uint64_t)];

        auto iv  = fc::sha256::hash( (char*)in_buffer, sizeof(in_buffer) );
        auto key = fc::sha512::hash( (char*)in_buffer, sizeof(in_buffer) );
        
         // aes hardware acceleration minimizes gap between CPU and ASIC
         fc::aes_encrypt( (unsigned char*)in_buffer, sizeof(in_buffer), (unsigned char*)&key, (unsigned char*)&iv, (unsigned char*)out_buffer );

         for( int32_t i = 0; i < 4;  )
         {
             fc::aes_encrypt( (unsigned char*)out_buffer, sizeof(in_buffer), (unsigned char*)&key, (unsigned char*)&iv, (unsigned char*)in_buffer );
             fc::aes_encrypt( (unsigned char*)in_buffer, sizeof(in_buffer), (unsigned char*)&key, (unsigned char*)&iv, (unsigned char*)out_buffer );

             // unpredictable run time makes GPU and pipelining less effecient
             i += 1 - out_buffer[30]%3;
         }
         out_buffer[31] >>= 30;
         return out_buffer[31];
   }

   namespace detail
   {
        class engine_impl
        {
            public:
               engine_impl():_delegate(nullptr),_thread("momentum"){}

               pow_seed_type                          _seed;
               engine_delegate*                       _delegate;
               volatile float                         _effort;
               std::unordered_map<uint64_t,uint64_t>  _result_to_nonce;

               fc::thread                             _thread;
               volatile uint64_t                      _count;

               void exec(uint64_t count)
               {
                    uint64_t nonce = 0;
                    while( count == _count && _effort > 0 )
                    {
                        auto birthday = pow_birthday_hash( _seed, nonce );
                        auto itr = _result_to_nonce.find(birthday);
                        if( itr != _result_to_nonce.end() )
                        {
                           fc::ripemd160::encoder enc;
                           enc.write( (char*)&nonce, sizeof(nonce) );
                           enc.write( (char*)&itr->second, sizeof(nonce) );
                           enc.write( (char*)&_seed, sizeof(_seed) );
                           _delegate->found_match( enc.result(), nonce, itr->second );
                        }
                        else
                        {
                          _result_to_nonce[birthday] = nonce;
                        }
                        if( _effort < 1 ) fc::usleep( fc::microseconds(1000*1000*(1-_effort)) );
                    }
               }
        };
   };

   engine::engine()
   :my( new detail::engine_impl() )
   {
   }

   void engine::set_delegate( engine_delegate* ed )
   {
        my->_delegate = ed;
   }

   void engine::start( const pow_seed_type& seed, float effort )
   {
        auto count = ++my->_count;
        
        my->_thread.async( [=](){
              my->_effort = effort;
              my->_seed   = seed;
              my->_result_to_nonce.clear();
              my->exec(count);
                           });

   }
   void engine::stop()
   {
        my->_effort = 0;
   }

}
