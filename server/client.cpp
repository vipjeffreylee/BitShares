#include "connection.hpp"
#include <bts/blockchain/blockchain_wallet.hpp>
#include <bts/blockchain/blockchain_db.hpp>
#include <bts/rpc/rpc_server.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/network/tcp_socket.hpp>
#include <fc/rpc/json_connection.hpp>
#include <fc/thread/thread.hpp>
#include <fc/network/resolve.hpp>
#include <fc/io/json.hpp>

#include <iostream>

struct config 
{
   config()
   :rpc_port(9898){}

   fc::string    host;
   fc::path      datadir;
   uint16_t      rpc_port;
   std::string   rpc_user;
   std::string   rpc_password;
};

FC_REFLECT( config, (host)(datadir)(rpc_port)(rpc_user)(rpc_password) )

class client
{
   public:
         ~client()
         {
              try {
                  _tcp_serv.close();
                  if( _accept_loop_complete.valid() )
                  {
                     _accept_loop_complete.cancel();
                     _accept_loop_complete.wait();
                  }
              } 
              catch ( const fc::canceled_exception& e ){}
              catch ( const fc::exception& e )
              {
                 wlog( "unhandled exception thrown in destructor.\n${e}", 
                       ("e", e.to_detail_string() ) );
              }
         }
         fc::tcp_server      _tcp_serv;
         fc::future<void>    _accept_loop_complete;
         config              _config;

         /** the set of connections that have successfully logged in */
         std::unordered_set<fc::rpc::json_connection*> _login_set;

         void accept_loop()
         {
           while( !_accept_loop_complete.canceled() )
           {
              fc::tcp_socket_ptr sock = std::make_shared<fc::tcp_socket>();
              _tcp_serv.accept( *sock );

              auto buf_istream = std::make_shared<fc::buffered_istream>( sock );
              auto buf_ostream = std::make_shared<fc::buffered_ostream>( sock );

              auto json_con = std::make_shared<fc::rpc::json_connection>( 
                                           std::move(buf_istream), 
                                           std::move(buf_ostream) );

              register_methods( json_con );

              fc::async( [json_con]{ json_con->exec().wait(); } );
           }
         }

         void register_methods( const fc::rpc::json_connection_ptr& con )
         {
            ilog( "login!" );
            // don't capture the shared ptr, it would create a circular reference
            fc::rpc::json_connection* capture_con = con.get(); 
            con->add_method( "login", [=]( const fc::variants& params ) -> fc::variant 
            {
                FC_ASSERT( params.size() == 2 );
                FC_ASSERT( params[0].as_string() == _config.rpc_user )
                FC_ASSERT( params[1].as_string() == _config.rpc_password )
                _login_set.insert( capture_con );
                return fc::variant( true );
            });

            // return new address
            con->add_method( "getnewaddress", [=]( const fc::variants& params ) -> fc::variant 
            {
                check_login( capture_con );
                auto addr = this->wallet.get_new_address();
                return fc::variant( std::string(addr) );
            });
             // double amount
             // string asset type
             // string address
             //
             // return trx id
             //
            con->add_method( "sendtoaddress", [=]( const fc::variants& params ) -> fc::variant 
            {
                check_login( capture_con );
                FC_ASSERT( params.size() == 3 );
                double         amount    = params[0].as_double();
                uint64_t fixed_amount    = uint64_t(amount * COIN);
                auto             unit    = params[1].as<asset::type>();
                std::string      addr    = params[2].as_string();
                auto trx = wallet.transfer( asset(fixed_amount,unit), bts::address(addr) );

                server_con.send( transaction_message( trx ) );
                return fc::variant( trx.id() );
            });
            con->add_method( "importkey", [=]( const fc::variants& params ) -> fc::variant 
            {
                check_login( capture_con );
                FC_ASSERT( params.size() == 1 );
                auto key    = params[0].as<fc::ecc::private_key>();
                wallet.import_key( key );
                return fc::variant();
            });

            // call this after importing a key to check for any outputs.
            con->add_method( "rescanchain", [=]( const fc::variants& params ) -> fc::variant 
            {
                check_login( capture_con );
                wallet.scan_chain( chain, 0 );
                return fc::variant();
            });

         } // register_methods

         void check_login( fc::rpc::json_connection* con )
         {
            if( _login_set.find( con ) == _login_set.end() )
            {
               FC_THROW_EXCEPTION( exception, "not logged in" ); 
            }
         }

         void configure( const config& cfg )
         {
            try {
              FC_ASSERT( cfg.rpc_port != 0 );
              _config = cfg;
              ilog( "listening for rpc connections on port ${port}", ("port",cfg.rpc_port) );
              _tcp_serv.listen( cfg.rpc_port );
            
              _accept_loop_complete = fc::async( [=]{ accept_loop(); } );

              chain.open( cfg.datadir / "blockchain" );
              wallet.open( cfg.datadir / "wallet.dat" );



            } FC_RETHROW_EXCEPTIONS( warn, "attempting to configure rpc server ${port}", 
                                            ("port",cfg.rpc_port)("config",cfg) );
         }

         void connect_loop()
         {
            while( true )
            {
               bool connected = false;
               auto sock = std::make_shared<fc::tcp_socket>();
               std::vector<fc::ip::endpoint> eps = fc::resolve( _config.host, 8901 );
               for( uint32_t i = 0; i < eps.size(); ++i )
               {
                  try {
                     ilog( "${ep}", ("ep",eps[i]) );
                     sock->connect_to( eps[i] );
                     server_con.sock = sock;
                     server_con.send( message(version_message(1)) );
                     server_con.send( message(get_block_message(chain.head_block_num())) );
                     process_connection();
                     break;
                  } 
                  catch(const fc::exception& e)
                  {
                     wlog( "${w}", ("w",e.to_detail_string() ) );
                     sock.reset();
                  }
               }
               fc::usleep( fc::seconds(10) );
            }
         }


         void exec()
         {
            while( true ) 
            {
              try { 
                 //fc::process_connection();
                 connect_loop();
              } 
              catch ( const fc::exception& e )
              {
                 wlog( "${e}", ("e",e.to_detail_string() ) );
                 fc::usleep( fc::seconds(5) );
              }
            }
         }

         void process_connection()
         {
             while( true )
             {
                auto msg = server_con.recv();
                switch( msg.msg_type.value )
                {
                    case version_msg:
                       FC_ASSERT( msg.as<version_message>().version == 1 );
                       break;
                    case get_block_msg:
                       wlog( "I don't serve no one" );
                       break;
                    case get_transaction_msg:
                       wlog( "I don't serve no one" );
                       break;
                    case block_msg:
                       // build block from pending trx
                       break;
                    case trx_block_msg:
                       // push block..
                       break;
                    case transaction_msg:
                       // pending trx insert
                       break;
                    default:
                       FC_ASSERT( !"Unknown Message Type", "Unknown message type ${msg}", ("msg",msg) );
                }
             }
         }

         connection                       server_con;
         bts::blockchain::blockchain_db   chain;
         bts::blockchain::wallet          wallet;
};

/**
 *  Client connects and subscribes starting at the last known block...
 *  the server will then stream everything the client needs and the client
 *  applies and verifies.
 */
int main( int argc, char** argv )
{
   try 
   {  
       if( argc < 2 )
       {
            std::cerr<<"Usage: "<<argv[0]<<" CONFIG\n";
            return -1;
       }
       client c;
       FC_ASSERT( fc::exists( argv[1] ), "${argv[1]}", ("argv[1]",std::string(argv[1])) );
       // load config file
       config cfg = fc::json::from_file(argv[1]).as<config>();

       c.configure(cfg);
       c.exec();
   }
   catch ( fc::exception& e )
   {
     std::cerr<<e.to_detail_string()<<"\n";
   }
   return 0;
}
