#include <fc/crypto/city.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/exception/exception.hpp>
#include <algorithm>
#include <locale>

namespace bts { namespace bitname {

/** valid chars:  A-Z 0-9 _ - . */
bool is_invalid_char( char c )
{
  return c == 0;
}

/** @note assumes c has already been converted to upper case asci  */
char replace_similar( char c )
{
  switch( c )
  {
     case 'H': return 'H';
     case '4': 
     case 'A': return 'A';


     case '6':
     case 'G': return 'G';

     case 'K': return 'K';

     case '9': // 9999PPP999999P99999P 
     case 'P': return 'P';

     case '2':
     case 'Z': 
     case '5':  
     case 'S': return 'S';

     case '7':
     case 'T': return 'T';

     case 'X': return 'X';
     case 'Y': return 'Y'; 

     case '3': 
     case 'E': return 'E';
     case 'F': return 'F';

     case 'L':
     case 'I':
     case '1': return 'I';
     case 'J': return 'J';

     case 'R': // when next to m or n is confusing ie: rn rm 
     case 'M':
     case 'N': return 'N';

     case 'C': return 'C';
     case '0': 
     case '8': // D 0 8 O B are all easily confused
     case 'B': 
     case 'D':
     case 'O':
     case 'Q': return 'O';

     case 'U':
     case 'W':
     case 'V': return 'U';

     case '.': 
     case '_': 
     case '-': return '.';

     default:
       return 0;
  }

}

void replace_char_runs( std::string& s )
{
  for( size_t p = 0; p < s.size() - 1; )
  {
    if( s[p] == s[p+1] )
    {
      s.erase(p,1);
    }
    else
    {
      ++p;
    }
  }
}

/**
 * @param n - the name in UTF-8 format
 */
uint64_t  name_hash( const std::string& n )
{
  if( n.size() == 0 ) return 0;

  // TODO: Use http://www.gnu.org/software/libidn/doxygen/ to convert Chinese to ASCII

  std::locale loc = std::locale::classic();
  std::string up(n);
  int length  = n.size();
  for(int i=0;i<length;++i)
    up[i] = std::toupper( n[i], loc );


  for( auto itr = up.begin(); itr != up.end(); ++itr )
    *itr = replace_similar(*itr);
 
  // remove any and all hidden or invalid characters
  up.erase(std::remove_if(up.begin(), up.end(), is_invalid_char), up.end());

  if( up.size() == 0 ) return 0;

  // replace NN UU ___ etc with a single instance to avoid any
  // confusion this way... yes this means mom, moon, noon will be the same.. boob, bob, bo will
  // all be treated the same, so one person can 'claim' all of those names with a single 
  // name registration. 
  replace_char_runs(up);

  if( up.size() && up.front() == '.' ) up.erase(0,1);
  if( up.size() && up.back() == '.' )  up.resize( up.size()-1);
  FC_ASSERT( up.size() > 0 );

  // secure hash function
  fc::sha256 h = fc::sha256::hash( up.c_str(), up.size() );

  // compress it down to 64 bits
  return fc::city_hash64( (char*)&h, sizeof(h) );
}

} } 
