#pragma once
#include <mail/message.hpp>
#include <mail/stcp_socket.hpp>
#include "chain_connection.hpp"
#include <bts/db/fwd.hpp>
#include <bts/config.hpp>

#include <set>


  namespace detail { class chain_server_impl; };

  class connection;
  typedef std::shared_ptr<chain_connection> connection_ptr;
  
  /**
   * @brief defines the set of callbacks that a server provides.
   *
   */
  class chain_server_delegate
  {
     public:
       virtual ~chain_server_delegate(){}

       virtual void on_connected( const connection_ptr& c ){}
       virtual void on_disconnected( const connection_ptr& c ){}
  };


  /**
   *   Abstracts the process of sending and receiving messages 
   *   on the network.  All messages are broadcast or received
   *   on a particular channel and each channel defines a protocol
   *   of message types supported on that channel.
   *
   *   The server will organize connections into a KAD tree for
   *   each subscribed channel.  The ID of a node will be the
   *   64 bit truncated SHA256(IP:PORT) which should distribute 
   *   peers well and facilitate certain types of protocols which
   *   do not want to broadcast everywhere, but instead to perform
   *   targeted lookup of data in a hash table.
   */
  class chain_server
  {
    public:
        struct config
        {
            config()
            :port(0){}
            uint16_t                 port;  ///< the port to listen for incoming connections on.
            std::vector<std::string> blacklist;  // host's that are blocked from connecting
            std::vector<fc::ip::endpoint> mirrors;  // host's that are blocked from connecting
        };
        
        chain_server();
        ~chain_server();

        void close();

        /**
         *  @note del must out live this server and the server does not
         *        take ownership of the delegate.
         */
        void set_delegate( chain_server_delegate* del );

        /**
         *  Sets up the ports and performs other one-time
         *  initialization.   
         */
        void configure( const config& c );

        /**
         * Get all connections for any channel.
         */
        std::vector<chain_connection_ptr> get_connections()const;

      private:
        std::unique_ptr<detail::chain_server_impl> my;
  };
  typedef std::shared_ptr<chain_server> chain_server_ptr;

FC_REFLECT( chain_server::config, (port)(mirrors) )
