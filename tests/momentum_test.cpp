#include <bts/momentum.hpp>
#include <fc/log/logger.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/thread/thread.hpp>

class pow_progress : public momentum::engine_delegate
{
    public:
      virtual void found_match( const momentum::pow_hash_type& result, uint64_t a, uint64_t b ) 
      {
         if( result < best )
         {
            best = result;
            ilog( "${result}  ${a} ${b}", ("result",result)("a",a)("b",b) );
         }  
      }
      momentum::pow_hash_type best;
};

int main( int argc, char** argv )
{
   fc::sha256 in;
   if( argc >= 2 )
      in = fc::sha256::hash(argv[1],strlen(argv[1]));

   pow_progress prog;
   memset((char*)&prog.best, 0xff, sizeof(prog.best) );
   momentum::engine e;
   e.set_delegate( &prog );
   e.start( in, 1 );
   fc::usleep( fc::seconds(10000) );
   return 0;
}
