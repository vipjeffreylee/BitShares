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
           std::unordered_map<uint32_t,wallet_contact>             _number_to_contact;
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

  const std::unordered_map<uint32_t,wallet_contact>& addressbook::get_contacts()const
  {
    return my->_number_to_contact;
  }

  void addressbook::open( const fc::path& abook_dir, const fc::uint512& key )
  { try {
     if( !fc::exists( abook_dir ) )
     {
        fc::create_directories( abook_dir );
     }
     my->_key = key;
     my->_encrypted_contact_db.open( abook_dir / "contact_db" );
     auto itr = my->_encrypted_contact_db.begin();
     while( itr.valid() )
     {
        auto cipher_data  = itr.value();
        try {
            auto packed_contact = fc::aes_decrypt( key, cipher_data );
            std::string json_contact = fc::raw::unpack<std::string>(packed_contact);
            ilog( "loading contact ${json}", ("json",json_contact) );
            auto next_contact = fc::json::from_string(json_contact).as<wallet_contact>();

            my->_number_to_contact[itr.key()] = next_contact;
            if( next_contact.public_key.valid() )
            {
                my->_key_to_number[next_contact.public_key.serialize()] = itr.key();
            }
            //if id_hash not cached in dbase (it isn't currently), then generate form dac_id_string
            if( next_contact.dac_id_hash == 0 )
                next_contact.dac_id_hash = bitname::name_hash(next_contact.dac_id_string);
            my->_id_to_number[next_contact.dac_id_hash] = itr.key();
        } 
        catch ( const fc::exception& e )
        {
            // TODO: redirect these warnings someplace useful... 
            wlog( "${e}", ("e",e.to_detail_string() ) );
        }
        ++itr;
     }

  } FC_RETHROW_EXCEPTIONS( warn, "", ("directory", abook_dir) ) }

  fc::optional<wallet_contact> addressbook::get_contact_by_dac_id( const std::string& dac_id )const
  { try {
      fc::optional<wallet_contact> con;
      auto dac_id_hash = bitname::name_hash(dac_id);
      auto itr = my->_id_to_number.find(dac_id_hash);
      if( itr != my->_id_to_number.end() )
      {
          return my->_number_to_contact[itr->second];
      }
      return con;
  } FC_RETHROW_EXCEPTIONS( warn, "", ("dac_id", dac_id) ) }

  fc::optional<wallet_contact> addressbook::get_contact_by_public_key( const fc::ecc::public_key& dac_id_key )const
  { try {
      auto itr = my->_key_to_number.find( dac_id_key.serialize() );
      if( itr != my->_key_to_number.end() )
      {
          return my->_number_to_contact[itr->second];
      }
      return fc::optional<wallet_contact>();
  } FC_RETHROW_EXCEPTIONS( warn, "", ("dac_id_key", dac_id_key) ) }

  void    addressbook::store_contact( const wallet_contact& contact_param )
  { try {
      FC_ASSERT( contact_param.wallet_index != WALLET_INVALID_INDEX ); 

      ilog( "to_string()" );
      std::string json_contact         = fc::json::to_string(contact_param);
      ilog( "... did it work?" );
      std::vector<char> packed_contact = fc::raw::pack(json_contact);
      std::vector<char> cipher_contact = fc::aes_encrypt( my->_key, packed_contact );
      my->_encrypted_contact_db.store( contact_param.wallet_index, cipher_contact );
      my->_number_to_contact[contact_param.wallet_index] = contact_param;
      if( contact_param.public_key.valid() )
      {
        my->_key_to_number[contact_param.public_key.serialize()] = contact_param.wallet_index;
      }
      if( contact_param.dac_id_hash != 0 )
      {
        my->_id_to_number[contact_param.dac_id_hash] = contact_param.wallet_index;
      }
  } FC_RETHROW_EXCEPTIONS( warn, "") }//, ("contact", contact_param) ) }

} } // bts::addressbook
