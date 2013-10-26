#include <bts/momentum.hpp>
#include <fc/log/logger.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/crypto/sha512.hpp>
#include <fc/thread/thread.hpp>

#include  <fc/reflect/variant.hpp>

int main( int argc, char** argv )
{
    try {
   fc::sha256 in;
   if( argc >= 2 )
      in = fc::sha256::hash(argv[1],strlen(argv[1]));

   /*
   auto start = fc::time_point::now();
   for( uint64_t i = 0; i < 2000000; ++i )
   {
      fc::sha512::encoder enc;
      enc.write( (char*)&i, sizeof(i) );
      auto tmp = enc.result();
   }
   auto end = fc::time_point::now();
   ilog( "elapsed: ${T}/sec", ("T", ((end-start).count())/1000000.0 ) );
   */

   auto results = bts::momentum_search( in );
   ilog( "${results} ", ("results",results) );
   for( auto itr = results.begin(); itr != results.end(); ++itr )
   {
        FC_ASSERT( bts::momentum_verify( in, itr->first, itr->second ) );
   }
    } catch ( const fc::exception& e )
    {
        elog( "${e}", ("e", e.to_detail_string() ) );
    }
   return 0;
}
