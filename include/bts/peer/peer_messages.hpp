#pragma once
#include <bts/network/channel_id.hpp>
#include <bts/peer/peer_host.hpp>
#include <fc/time.hpp>
#include <fc/network/ip.hpp>
#include <unordered_set>

namespace bts { namespace peer {
  /// these belong as part of the peer proto channel, not part of
  ///  the message class.
  enum message_code
  {
     generic         = 0,
     config          = 1,
     announce        = 2,
     known_hosts     = 3,
     error_report    = 4,
     subscribe       = 5,
     unsubscribe     = 6,
     get_known_hosts = 7,
     get_subscribed  = 8
  };

  struct config_msg
  {
      static const message_code type = message_code::config;
      /** 
       *  A list of features supported by this client.
       */
      std::unordered_set<std::string> supported_features;
      fc::ip::endpoint                public_contact;
      fc::time_point                  timestamp;
  };


  struct known_hosts_msg
  {
     static const message_code type = message_code::known_hosts;
      known_hosts_msg(){}
      known_hosts_msg( std::vector<host> h )
      :hosts( std::move(h) ){}
      std::vector<host> hosts;
  };

  /**
   *  When a new node connects to the network, they can broadcast
   *  their IP and features so that other nodes can connect to them.  Because
   *  broadcasts can be expensive, connect messages 
   */
  struct announce_msg : public config_msg
  {
     static const message_code type = message_code::announce;
      bool     validate_work()const;
      void     find_birthdays();

      uint32_t        birthday_a;
      uint32_t        birthday_b;
  };

  struct subscribe_msg
  {
     static const message_code type = message_code::subscribe;
     subscribe_msg(){}
     subscribe_msg( std::vector<network::channel_id> chans )
     :channels( std::move(chans) ){}

     std::vector<network::channel_id> channels;
  };

  struct unsubscribe_msg
  {
     static const message_code type = message_code::unsubscribe;
     std::vector<network::channel_id> channels;
  };

  struct get_subscribed_msg 
  {
     static const message_code type = message_code::get_subscribed;
  };

  struct get_known_hosts_msg
  {
      static const message_code type = message_code::get_known_hosts;
      get_known_hosts_msg(){}
  };

  struct error_report_msg
  {
     static const message_code type = message_code::error_report;
     uint32_t     code;
     std::string  message;
  };

} } // bts::peer

#include <fc/reflect/reflect.hpp>
FC_REFLECT( bts::peer::config_msg,       
    (supported_features)
    (public_contact)
    (timestamp)
    )

FC_REFLECT_ENUM( bts::peer::message_code,
  (generic)
  (config)
  (known_hosts)
  (error_report)
  (subscribe)
  (unsubscribe)
  (get_known_hosts)
  (get_subscribed)
  )
FC_REFLECT( bts::peer::known_hosts_msg,  (hosts) )
FC_REFLECT( bts::peer::error_report_msg, (code)(message) )
FC_REFLECT_DERIVED( bts::peer::announce_msg, (bts::peer::config_msg), (birthday_a)(birthday_b) )
FC_REFLECT( bts::peer::subscribe_msg,    (channels ) )
FC_REFLECT( bts::peer::unsubscribe_msg,  (channels ) )
FC_REFLECT( bts::peer::get_known_hosts_msg, BOOST_PP_SEQ_NIL ); 

