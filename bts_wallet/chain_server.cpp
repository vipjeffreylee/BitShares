#include "chain_server.hpp" 
#include "chain_connection.hpp"
#include <mail/message.hpp>
#include <mail/stcp_socket.hpp>
#include <bts/blockchain/blockchain_db.hpp>
#include <bts/db/level_map.hpp>
#include <fc/time.hpp>
#include <fc/network/tcp_socket.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/thread/thread.hpp>
#include <fc/thread/future.hpp>
#include <fc/io/raw.hpp>
#include <fc/log/logger.hpp>


#include <algorithm>
#include <unordered_map>
#include <map>

using namespace mail;

fc::ecc::private_key test_genesis_private_key()
{
    return fc::ecc::private_key::generate_from_seed( fc::sha256::hash( "genesis", 7 ) );
}

bts::blockchain::trx_block create_test_genesis_block()
{
   bts::blockchain::trx_block b;
   b.version      = 0;
   b.prev         = bts::blockchain::block_id_type();
   b.block_num    = 0;
   b.total_shares = 100*COIN;
   b.timestamp    = fc::time_point::from_iso_string("20131201T054434");

   bts::blockchain::signed_transaction coinbase;
   coinbase.version = 0;
   //coinbase.valid_after = 0;
   //coinbase.valid_blocks = 0;

   // TODO: init from PTS here...
   coinbase.outputs.push_back( 
      bts::blockchain::trx_output( bts::blockchain::claim_by_signature_output( bts::address(test_genesis_private_key().get_public_key()) ), b.total_shares, bts::blockchain::asset::bts) );

   b.trxs.emplace_back( std::move(coinbase) );
   b.trx_mroot   = b.calculate_merkle_root();

   return b;
}

namespace detail
{
   class chain_server_impl : public chain_connection_delegate
   {
      public:
        chain_server_impl()
        :ser_del(nullptr)
        {}

        ~chain_server_impl()
        {
           close();
        }
        void close()
        {
            ilog( "closing connections..." );
            try 
            {
                for( auto i = pending_connections.begin(); i != pending_connections.end(); ++i )
                {
                  (*i)->close();
                }
                tcp_serv.close();
                if( accept_loop_complete.valid() )
                {
                    accept_loop_complete.cancel();
                    accept_loop_complete.wait();
                }
            } 
            catch ( const fc::canceled_exception& e )
            {
                ilog( "expected exception on closing tcp server\n" );  
            }
            catch ( const fc::exception& e )
            {
                wlog( "unhandled exception in destructor ${e}", ("e", e.to_detail_string() ));
            } 
            catch ( ... )
            {
                elog( "unexpected exception" );
            }
        }
        chain_server_delegate*                                      ser_del;
        fc::ip::address                                             _external_ip;
        std::unordered_map<fc::ip::endpoint,chain_connection_ptr>   connections;

        std::set<chain_connection_ptr>                              pending_connections;
        chain_server::config                                        cfg;
        fc::tcp_server                                              tcp_serv;
                                                                   
        fc::future<void>                                            accept_loop_complete;
                                                                   
        /**
         *  This is called every time a message is received from c, there are only two
         *  messages supported:  seek to time and broadcast.  When a message is 
         *  received it goes into the database which all of the connections are 
         *  reading from and sending to their clients.
         *
         *  The difficulty required adjusts every 5 minutes with the goal of maintaining
         *  an average data rate of 1.5 kb/sec from all connections.
         */
        virtual void on_connection_message( chain_connection& c, const message& m )
        {
             
             // TODO: perhaps do this ASYNC?
             // itr->second->handle_message( c.shared_from_this(), m );
             /*
             if( m.type == bts::bitchat::encrypted_message::type )
             {
                 auto pm = m.as<bts::bitchat::encrypted_message>();
                 if( pm.validate_proof() )
                 {
                    _message_db.store( fc::time_point::now(), pm );
                 }
             }
             else if( m.type == bts::bitchat::client_info_message::type )
             {
                 auto ci = m.as<bts::bitchat::client_info_message>();
                 if( c.get_last_sync_time() == fc::time_point() )
                 {
                    c.exec_sync_loop();
                 }
                 c.set_last_sync_time( ci.sync_time );
             }
             else
             {
                 c.close();
             }
             */
        }


        virtual void on_connection_disconnected( chain_connection& c )
        {
          try {
            ilog( "cleaning up connection after disconnect ${e}", ("e", c.remote_endpoint()) );
            auto cptr = c.shared_from_this();
            FC_ASSERT( cptr );
            if( ser_del ) ser_del->on_disconnected( cptr );
            auto itr = connections.find(c.remote_endpoint());
            connections.erase( itr ); //c.remote_endpoint() );
          } FC_RETHROW_EXCEPTIONS( warn, "error thrown handling disconnect" );
        }

        /**
         *  This method is called via async from accept_loop and
         *  should not throw any exceptions because they are not
         *  being caught anywhere.
         *
         *  
         */
        void accept_connection( const stcp_socket_ptr& s )
        {
           try 
           {
              // init DH handshake, TODO: this could yield.. what happens if we exit here before
              // adding s to connections list.
              s->accept();
              ilog( "accepted connection from ${ep}", 
                    ("ep", std::string(s->get_socket().remote_endpoint()) ) );
              
              auto con = std::make_shared<chain_connection>(s,this);
              connections[con->remote_endpoint()] = con;
              con->set_database( &chain );
              if( ser_del ) ser_del->on_connected( con );
           } 
           catch ( const fc::canceled_exception& e )
           {
              ilog( "canceled accept operation" );
           }
           catch ( const fc::exception& e )
           {
              wlog( "error accepting connection: ${e}", ("e", e.to_detail_string() ) );
           }
           catch( ... )
           {
              elog( "unexpected exception" );
           }
        }

        /**
         *  This method is called async 
         */
        void accept_loop() throw()
        {
           try
           {
              while( !accept_loop_complete.canceled() )
              {
                 stcp_socket_ptr sock = std::make_shared<stcp_socket>();
                 tcp_serv.accept( sock->get_socket() );

                 // do the acceptance process async
                 fc::async( [=](){ accept_connection( sock ); } );

                 // limit the rate at which we accept connections to prevent
                 // DOS attacks.
                 fc::usleep( fc::microseconds( 1000*10 ) );
              }
           } 
           catch ( fc::eof_exception& e )
           {
              ilog( "accept loop eof" );
           }
           catch ( fc::canceled_exception& e )
           {
              ilog( "accept loop canceled" );
           }
           catch ( fc::exception& e )
           {
              elog( "tcp server socket threw exception\n ${e}", 
                                   ("e", e.to_detail_string() ) );
              // TODO: notify the server delegate of the error.
           }
           catch( ... )
           {
              elog( "unexpected exception" );
           }
        }
        bts::blockchain::blockchain_db chain;
   };
}




chain_server::chain_server()
:my( new detail::chain_server_impl() ){}

chain_server::~chain_server()
{ }


void chain_server::set_delegate( chain_server_delegate* sd )
{
   my->ser_del = sd;
}

void chain_server::configure( const chain_server::config& c )
{
  try {
     my->cfg = c;
     
     ilog( "listening for stcp connections on port ${p}", ("p",c.port) );
     my->tcp_serv.listen( c.port );
     my->accept_loop_complete = fc::async( [=](){ my->accept_loop(); } ); 
     
     my->chain.open( "chain" );
     if( my->chain.head_block_num() == uint32_t(-1) )
     {
         auto genesis = create_test_genesis_block();
         //ilog( "genesis block: \n${s}", ("s", fc::json::to_pretty_string(genesis) ) );
         my->chain.push_block( genesis );
     }

  } FC_RETHROW_EXCEPTIONS( warn, "error configuring server", ("config", c) );
}

std::vector<chain_connection_ptr> chain_server::get_connections()const
{ 
    std::vector<chain_connection_ptr>  cons; 
    cons.reserve( my->connections.size() );
    for( auto itr = my->connections.begin(); itr != my->connections.end(); ++itr )
    {
      cons.push_back(itr->second);
    }
    return cons;
}

/*
void chain_server::broadcast( const message& m )
{
    for( auto itr = my->connections.begin(); itr != my->connections.end(); ++itr )
    {
      try {
         itr->second->send(m);
      } 
      catch ( const fc::exception& e ) 
      {
         // TODO: propagate this exception back via the delegate or some other means... don't just fail
         wlog( "exception thrown while broadcasting ${e}", ("e", e.to_detail_string() ) );
      }
    }
}
*/

void chain_server::close()
{
  try {
    my->close();
  } FC_RETHROW_EXCEPTIONS( warn, "error closing server socket" );
}

