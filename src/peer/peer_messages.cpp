#include <bts/peer/peer_messages.hpp>
#include <bts/momentum.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/io/raw.hpp>
#include <fc/crypto/city.hpp>

namespace bts { namespace peer {
  
const message_code config_msg::type;
const message_code announce_msg::type;
const message_code known_hosts_msg::type;
const message_code subscribe_msg::type;
const message_code unsubscribe_msg::type;
const message_code get_subscribed_msg::type;
const message_code error_report_msg::type;
const message_code get_known_hosts_msg::type;
const message_code announce_inv_msg::type;
const message_code get_announce_msg::type;

announce_msg::announce_msg()
:birthday_a(0),birthday_b(0),_cancel_birthday_search(false){}

uint64_t config_msg::get_host_id()const
{
   uint32_t addr = public_contact.get_address();
   return fc::city_hash64( (char*)&addr, sizeof(addr) );
}

/** returns a hash for this announcement used when broadcasting the
 * message on the network. 
 */
uint64_t  announce_msg::announce_id()const
{
   auto vec = fc::raw::pack(*this);
   return fc::city_hash64( vec.data(), vec.size() );
}

bool  announce_msg::validate_work()const
{
   if( (fc::time_point::now() - timestamp) > fc::seconds(120) )
      return false;

   if( (timestamp - fc::time_point::now()) > fc::seconds(120) )
      return false;

   fc::sha256::encoder enc;
   fc::raw::pack( enc, static_cast<const config_msg&>(*this) );
   auto seed = enc.result();

   if( !momentum_verify( seed, birthday_a, birthday_a ) )
   {
      return false;
   }

   fc::sha256::encoder enc2;
   fc::raw::pack( enc2, *this );
   auto msg_hash = enc2.result();
   char* msg_hash_ptr = (char*)&msg_hash;
   return msg_hash_ptr[0] == 0;
}

void announce_msg::stop_birthday_search()
{
    _cancel_birthday_search = true;
}

void  announce_msg::find_birthdays()
{
   _cancel_birthday_search = false;
   while( !_cancel_birthday_search && !validate_work() )
   {
      timestamp = fc::time_point::now();
      fc::sha256::encoder enc;
      fc::raw::pack( enc, static_cast<const config_msg&>(*this) );
      auto seed = enc.result();

      auto opts = momentum_search( seed );
      for( auto itr = opts.begin(); itr != opts.end(); ++itr )
      {
          birthday_a = itr->first;
          birthday_b = itr->second;
          if( validate_work() ) return;
      }
   }
}

} } // bts::peer
