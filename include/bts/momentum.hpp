#pragma once
#include <fc/array.hpp>
#include <fc/io/varint.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <fc/reflect/reflect.hpp>

namespace momentum 
{
   typedef fc::sha256     pow_seed_type;
   typedef fc::ripemd160  pow_hash_type;

   namespace detail { class engine_impl; }

   class engine_delegate
   {
        public:
           virtual ~engine_delegate(){}
           /**
            *  This method is called every time nonce a and b result in a partial birthday collision when
            *  hashed with the seed passed to start()
            */
           virtual void found_match( const pow_hash_type& result, uint64_t a, uint64_t b ) = 0;
   };

   class engine
   {
      public:
         engine();
         ~engine();

         void set_delegate( engine_delegate* del );
         void start( const pow_seed_type& s, float effort = 1.0 );
         void stop();

         pow_hash_type verify( const pow_seed_type& s, uint64_t nonce_a, uint64_t nonce_b );

      private:
         std::unique_ptr<detail::engine_impl> my;
   };

   uint64_t pow_birthday_hash( const pow_seed_type& s, uint64_t nonce );

    
   struct pow_data
   {
      pow_data():nonce_a(0),nonce_b(0){}
      std::vector<fc::unsigned_int>  chains;
      fc::unsigned_int               leaf_num;
      std::vector<pow_hash_type>     merkel_branch;
      uint64_t                       nonce_a;
      uint64_t                       nonce_b;

      /** given the chain ID and header hash, calculate the merkle root of the pow */
      pow_hash_type verify( fc::unsigned_int chain, pow_hash_type header_hash )const;
   };
};

FC_REFLECT( momentum::pow_data, (chains)(leaf_num)(merkel_branch)(nonce_a)(nonce_b) )
