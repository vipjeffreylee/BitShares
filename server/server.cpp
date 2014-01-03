#include "messages.hpp"
#include "connection.hpp"
#include <bts/config.hpp>
#include <fc/thread/thread.hpp>
#include <fc/log/logger.hpp>
#include <fc/network/ip.hpp>

#include <unordered_map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <fc/io/raw.hpp>
#include <bts/blockchain/blockchain_db.hpp>

class server 
{
   public:
      fc::thread*                                       main_thread;
      fc::tcp_server                                    tcp_serv;
      std::unordered_map<fc::ip::endpoint,connection>   connections;
      fc::future<void>                                  accept_loop_complete;
      bts::blockchain::blockchain_db                    chain;

      
      void init_chain()
      {
           // build genesis block if there is no chain data 
           if( chain.head_block_num() == INVALID_BLOCK_NUM )
           {
               bts::blockchain::trx_block b;
               b.version      = 0;
               b.prev         = block_id_type();
               b.block_num    = 0;
               b.timestamp    = fc::time_point::from_iso_string("20131201T054434");
               b.trxs.emplace_back( load_genesis( "genesis.csv", b.total_shares) );
               b.trx_mroot   = b.calculate_merkle_root();

               chain.push_block(b);
           }
      }

      signed_transaction load_genesis( const fc::path& csv, uint64_t& total_coins )
      {
          total_coins = 0;
          signed_transaction coinbase;
          coinbase.version = 0;
        //  coinbase.valid_after = 0;
        //  coinbase.valid_blocks = 0;

          std::ifstream in(csv.generic_string().c_str(), std::ios::binary);
          std::string line;
          std::getline( in, line );
          while( in.good() )
          {
            std::stringstream ss(line);
            std::string addr;
            uint64_t amnt;
            ss >> addr >> amnt;
            total_coins += amnt;
            coinbase.outputs.push_back( 
              trx_output( claim_by_pts_output( bts::pts_address(addr) ), amnt, asset::bts) );
            std::getline( in, line );
          }

          return coinbase;
      }

      void process_connection( connection con )
      {
          try 
          {
             con.send( message(version_message(1)) );

             while( true )
             {
                 auto msg = con.recv();
                 switch( msg.msg_type.value )
                 {
                    case version_msg:
                       FC_ASSERT( msg.as<version_message>().version == 1 );
                       break;
                    case get_block_msg:
                       break;
                    case get_transaction_msg:
                       break;
                    case block_msg:
                       wlog( "No one sends me blocks!" );
                       break;
                    case trx_block_msg:
                       wlog( "No one sends me blocks!" );
                       break;
                    case transaction_msg:
                       break;
                    default:
                       FC_ASSERT( !"Unknown Message Type", "Unknown message type ${msg}", ("msg",msg) );
                 }
             }
          } 
          catch ( const fc::exception& e )
          {
               connections.erase( con.sock->remote_endpoint() );
          }
      }


      void accept_connection( const fc::tcp_socket_ptr& s )
      {
         try 
         {
            // init DH handshake, TODO: this could yield.. what happens if we exit here before
            // adding s to connections list.
            ilog( "accepted connection from ${ep}", 
                  ("ep", std::string(s->remote_endpoint()) ) );
            
            connections[s->remote_endpoint()] = connection(s);
            fc::async( [=](){ process_connection( connections[s->remote_endpoint()] ); } );
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

      void accept_loop() throw()
      {
         try
         {
            while( !accept_loop_complete.canceled() )
            {
               auto sock = std::make_shared<fc::tcp_socket>();
               tcp_serv.accept( *sock );

               // do the acceptance process async
               fc::async( [=](){ accept_connection( sock ); } );

               fc::usleep(fc::microseconds(1000) );
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
};


int main( int argc, char** argv )
{
   try 
   {  
      if( argc < 2 )
      {
         std::cerr<<"usage: "<<argv[0]<<" DATADIR\n";
         return -1;
      }
      server serv;
      serv.chain.open( fc::path(argv[1])/"chain" );
      serv.tcp_serv.listen( 8901 );
      serv.init_chain();
       
      serv.accept_loop_complete = fc::async( [&](){ serv.accept_loop(); } );
      serv.accept_loop_complete.wait();
   } 
   catch ( fc::exception& e )
   {
     std::cerr<<e.to_detail_string()<<"\n";
   }
   return 0;
}
