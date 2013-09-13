#include <bts/addressbook/addressbook.hpp>
#include <bts/db/level_pod_map.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/filesystem.hpp>
#include <fc/io/raw.hpp>
#include <fc/io/json.hpp>
#include <fc/crypto/aes.hpp>
#include <unordered_map>

#include <fc/log/logger.hpp>
#include <bts/bitname/bitname_hash.hpp>

namespace bts { namespace addressbook {

  namespace detail 
  { 
     class addressbook_impl
     {
        public:
           fc::uint512                                             _key;
           db::level_pod_map<uint32_t,std::vector<char> >          _encrypted_contact_db;
           std::unordered_map<uint32_t,contact>                    _number_to_contact;
           std::unordered_map<fc::ecc::public_key_data,uint32_t>   _key_to_number;
           std::unordered_map<uint64_t,uint32_t>                   _id_to_number;
     };
  }

  addressbook::addressbook()
  :my( new detail::addressbook_impl() )
  {
  }

  addressbook::~addressbook()
  {
  }

  const std::unordered_map<uint32_t,contact>& addressbook::get_contacts()const
  {
    return my->_number_to_contact;
  }

  void addressbook::open( const fc::path& abook_dir, const fc::uint512& key )
  { try {
     if( !fc::exists( abook_dir ) )
     {
        fc::create_directories( abook_dir );
     }
     my->_encrypted_contact_db.open( abook_dir / "contact_db" );
     auto itr = my->_encrypted_contact_db.begin();
     while( itr.valid() )
     {
        auto cipher_data  = itr.value();
        try {
            auto packed_contact = fc::aes_decrypt( key, cipher_data );
            std::string json_contact = fc::raw::unpack<std::string>(packed_contact);
            auto next_contact = fc::json::from_string(json_contact).as<contact>();

            my->_number_to_contact[itr.key()] = next_contact;
            if( next_contact.send_msg_address.valid() )
            {
                my->_key_to_number[next_contact.send_msg_address.serialize()] = itr.key();
            }
            if( next_contact.bit_id_hash != 0 )
            {
                my->_id_to_number[next_contact.bit_id_hash] = itr.key();
            }
        } 
        catch ( const fc::exception& e )
        {
            // TODO: redirect these warnings someplace useful... 
            wlog( "${e}", ("e",e.to_detail_string() ) );
        }
     }

  } FC_RETHROW_EXCEPTIONS( warn, "", ("directory", abook_dir) ) }

  std::vector<std::string> addressbook::get_known_bitnames()const
  {
      std::vector<std::string> known_bitnames;
      known_bitnames.reserve( my->_id_to_number.size() );
      for( auto itr = my->_id_to_number.begin(); itr != my->_id_to_number.end(); ++itr )
      {
        known_bitnames.push_back( my->_number_to_contact[itr->second].bit_id ); 
      }
      return known_bitnames;
  }

  fc::optional<contact> addressbook::get_contact_by_bitname( const std::string& bitname_id )const
  { try {
      fc::optional<contact> con;
      auto bitid_hash = bitname::name_hash(bitname_id);
      auto itr = my->_id_to_number.find(bitid_hash);
      if( itr != my->_id_to_number.end() )
      {
          return my->_number_to_contact[itr->second];
      }
      return con;
  } FC_RETHROW_EXCEPTIONS( warn, "", ("bitname_id", bitname_id) ) }

  std::string addressbook::get_bitname_by_address( const fc::ecc::public_key& bitname_key )const
  { try {
      auto itr = my->_key_to_number.find( bitname_key.serialize() );
      if( itr != my->_key_to_number.end() )
      {
          return my->_number_to_contact[itr->second].bit_id;
      }
      return std::string();
  } FC_RETHROW_EXCEPTIONS( warn, "", ("bitname_key", bitname_key) ) }

  void    addressbook::store_contact( const contact& contact_param )
  { try {
      FC_ASSERT( contact_param.wallet_account_index != uint32_t(-1) ); // TODO: replace magic number

      std::string json_contact         = fc::json::to_string(contact_param);
      std::vector<char> packed_contact = fc::raw::pack(json_contact);
      std::vector<char> cipher_contact = fc::aes_encrypt( my->_key, packed_contact );
      my->_encrypted_contact_db.store( contact_param.wallet_account_index, cipher_contact );
      my->_number_to_contact[contact_param.wallet_account_index] = contact_param;
      if( contact_param.send_msg_address.valid() )
      {
        my->_key_to_number[contact_param.send_msg_address.serialize()] = contact_param.wallet_account_index;
      }
      if( contact_param.bit_id_hash != 0 )
      {
        my->_id_to_number[contact_param.bit_id_hash] = contact_param.wallet_account_index;
      }
  } FC_RETHROW_EXCEPTIONS( warn, "", ("contact", contact_param) ) }

} } // bts::addressbook
