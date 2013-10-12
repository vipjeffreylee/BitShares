#include <bts/momentum.hpp>
#include <fc/log/logger.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/thread/thread.hpp>

class pow_progress : public momentum::engine_delegate
{
    public:
      pow_progress():match_count(0){}
      virtual void found_match( const momentum::pow_hash_type& result, uint64_t a, uint64_t b ) 
      {
         ++match_count;
         if( match_count % 200 == 0 )
         {
            auto delta = fc::time_point::now() - last_best;
            last_best = fc::time_point::now();
            ilog( "match persec ${persec}",("persec", (1000000.0*match_count)/delta.count() ) );
            match_count = 0;
         }
         if( result < best )
         {
            best = result;
            ilog( "best: ${result}  ${a} ${b}", ("result",result)("a",a)("b",b) );
         }  
      }
      momentum::pow_hash_type best;
      fc::time_point          last_best;
      uint32_t                match_count;
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
