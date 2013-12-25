#include <bts/profile.hpp>
#include <bts/keychain.hpp>
#include <bts/addressbook/addressbook.hpp>
#include <bts/addressbook/contact.hpp>
#include <bts/db/level_map.hpp>
#include <bts/db/level_pod_map.hpp>
#include <fc/io/raw.hpp>
#include <fc/io/raw_variant.hpp>

#include <fc/crypto/aes.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/exception/exception.hpp>
#include <fc/io/fstream.hpp>
#include <fc/filesystem.hpp>

namespace bts {

  namespace detail 
  {
     class profile_impl
     {
        public:
            keychain                                        _keychain;
            addressbook::addressbook_ptr                    _addressbook;
            bitchat::message_db_ptr                         _inbox_db;
            bitchat::message_db_ptr                         _draft_db;
            bitchat::message_db_ptr                         _pending_db;
            bitchat::message_db_ptr                         _sent_db;
            bitchat::message_db_ptr                         _chat_db;
            db::level_map<std::string, addressbook::wallet_identity>            _idents;
            
/*
            void import_draft( const std::vector<char> crypt, const fc::uint512& key )
            {
                auto plain = fc::aes_decrypt( key, crypt );
                _drafts.push_back( fc::raw::unpack<bts::bitchat::email_draft>(plain) );
            }
*/
     };

  } // namespace detail

  profile::profile()
  :my( new detail::profile_impl() )
  {
    my->_addressbook = std::make_shared<addressbook::addressbook>();
 
    my->_inbox_db  = std::make_shared<bitchat::message_db>();
    my->_draft_db  = std::make_shared<bitchat::message_db>();
    my->_pending_db  = std::make_shared<bitchat::message_db>();
    my->_sent_db  = std::make_shared<bitchat::message_db>();
    my->_chat_db = std::make_shared<bitchat::message_db>();
  }
  

  profile::~profile()
  {}

  void profile::create( const fc::path& profile_dir, const profile_config& cfg, const std::string& password )
  { try {
       fc::sha512::encoder encoder;
       fc::raw::pack( encoder, password );
       fc::raw::pack( encoder, cfg );
       auto seed             = encoder.result();

       /// note: this could take a minute
       auto stretched_seed   = keychain::stretch_seed( seed );
       
      // FC_ASSERT( !fc::exists( profile_dir ) );
       fc::create_directories( profile_dir );
       
       auto profile_cfg_key  = fc::sha512::hash( password.c_str(), password.size() );
       fc::aes_save( profile_dir / ".stretched_seed", profile_cfg_key, fc::raw::pack(stretched_seed) );
  } FC_RETHROW_EXCEPTIONS( warn, "", ("profile_dir",profile_dir)("config",cfg) ) }

  void profile::open( const fc::path& profile_dir, const std::string& password )
  { try {
      fc::create_directories( profile_dir );
      fc::create_directories( profile_dir / "addressbook" );
      fc::create_directories( profile_dir / "idents" );
      fc::create_directories( profile_dir / "mail" );
      fc::create_directories( profile_dir / "mail" / "inbox" );
      fc::create_directories( profile_dir / "mail" / "draft" );
      fc::create_directories( profile_dir / "mail" / "pending" );
      fc::create_directories( profile_dir / "mail" / "sent" );
      fc::create_directories( profile_dir / "chat" );

      auto profile_cfg_key         = fc::sha512::hash( password.c_str(), password.size() );
      auto stretched_seed_data     = fc::aes_load( profile_dir / ".stretched_seed", profile_cfg_key );
     
      my->_keychain.set_seed( fc::raw::unpack<fc::sha512>(stretched_seed_data) );
      my->_addressbook->open( profile_dir / "addressbook", profile_cfg_key );
      my->_idents.open( profile_dir / "idents" );
      my->_inbox_db->open( profile_dir / "mail" / "inbox", profile_cfg_key );
      my->_draft_db->open( profile_dir / "mail" / "draft", profile_cfg_key );
      my->_pending_db->open( profile_dir / "mail" / "pending", profile_cfg_key );
      my->_sent_db->open( profile_dir / "mail" / "sent", profile_cfg_key );
      my->_chat_db->open( profile_dir / "chat", profile_cfg_key );

/*
      auto itr = my->_draft_db.begin();
      while( itr.valid() )
      {
          my->import_draft( itr.value(), profile_cfg_key );
          ++itr;
      }
*/
  } FC_RETHROW_EXCEPTIONS( warn, "", ("profile_dir",profile_dir) ) }

  std::vector<addressbook::wallet_identity>   profile::identities()const
  { try {
     std::vector<addressbook::wallet_identity> idents;
     for( auto itr = my->_idents.begin(); itr.valid(); ++itr )
     {
       idents.push_back(itr.value());
     }
     return idents;
  } FC_RETHROW_EXCEPTIONS( warn, "" ) }
  
  void    profile::store_identity( const addressbook::wallet_identity& id )
  { try {
      my->_idents.store( id.dac_id_string, id ); 
  } FC_RETHROW_EXCEPTIONS( warn, "", ("id",id) ) }
  
  /**
   *  Checks the transaction to see if any of the inp
   */
  //void  profile::cache( const bts::blockchain::meta_transaction& mtrx );
  void    profile::cache( const bts::bitchat::decrypted_message& msg    )
  { try {
    //my->_message_db->store( msg );
  } FC_RETHROW_EXCEPTIONS( warn, "", ("msg",msg)) }
  /*
  std::vector<meta_transaction> profile::get_transactions()const
  {
  }
  */

  bitchat::message_db_ptr profile::get_inbox_db() const { return my->_inbox_db; }
  bitchat::message_db_ptr profile::get_draft_db() const { return my->_draft_db; }
  bitchat::message_db_ptr profile::get_pending_db() const { return my->_pending_db; }
  bitchat::message_db_ptr profile::get_sent_db() const { return my->_sent_db; }
  bitchat::message_db_ptr profile::get_chat_db() const { return my->_chat_db; }

  addressbook::addressbook_ptr profile::get_addressbook() const { return my->_addressbook; }

  keychain profile::get_keychain() const { return my->_keychain; }
  /*
  const std::vector<bitchat::email_draft>&   profile::get_drafts()const
  {
      return my->_drafts;
  }
  void profile::save_draft( const bitchat::email_draft& draft )
  { try {
     
  } FC_RETHROW_EXCEPTIONS( warn, "", ("draft",draft) ) }

  void profile::delete_draft( uint32_t draft_id )
  { try {
     
  } FC_RETHROW_EXCEPTIONS( warn, "", ("draft",draft_id) ) }
*/

} // namespace bts
