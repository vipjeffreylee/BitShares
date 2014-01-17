#include <iostream>
#include <sstream>
#include <iomanip>
#include <fc/filesystem.hpp>
#include <bts/blockchain/blockchain_wallet.hpp>
#include <fc/thread/thread.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/log/file_appender.hpp>
#include <fc/log/logger_config.hpp>
#include <bts/config.hpp>
#include <fc/io/raw.hpp>
#include <fc/io/json.hpp>
#include <fc/log/logger.hpp>
#include <fstream>
#include <bts/blockchain/blockchain_printer.hpp>
#include "chain_connection.hpp"
#include "chain_messages.hpp"

using namespace bts::blockchain;

class client_delegate
{

};


std::string to_balance( uint64_t a )
{
    uint64_t fraction = a % COIN;
    auto fract_str = fc::to_string(static_cast<uint64_t>(fraction+COIN)).substr(1);
    return fc::to_string( uint64_t(a/COIN) ) + "." + fract_str;
}

class client : public chain_connection_delegate
{
   public:
      client():_chain_con(this),_chain_connected(false){}

      void open( const fc::path& datadir )
      {
          chain.open( datadir / "chain" );
          wallet.open( datadir / "wallet.bts" );

         // if( chain.head_block_num() == uint32_t(-1) )
         // {
          //    auto genesis = create_test_genesis_block();
         //   ilog( "genesis block: \n${s}", ("s", fc::json::to_pretty_string(genesis) ) );
         //    chain.push_block( genesis );
         //   wallet.import_key( test_genesis_private_key() );
         //     wallet.scan_chain( chain, 0 );
         //     wallet.save();
         // }
          wallet.scan_chain( chain );

          // load config, connect to server, and start subscribing to blocks...
          sim_loop_complete = fc::async( [this]() { server_sim_loop(); } );
          chain_connect_loop_complete = fc::async( [this](){ chain_connect_loop(); } );
      }

      void broadcast_transaction( const signed_transaction& trx )
      {
         trx_queue.push_back(trx);
      }

      void handle_block( uint32_t block_num )
      {
         wallet.set_stake( chain.get_stake() );
         wallet.scan_chain( chain, block_num );
      }

      void server_sim_loop()
      { 
        try {
           while( true )
           {
              fc::usleep( fc::seconds(20) );

              auto order_trxs   = chain.match_orders(); 
              trx_queue.insert( trx_queue.end(), order_trxs.begin(), order_trxs.end() );
              if( trx_queue.size() )
              {
                 auto new_block = chain.generate_next_block( trx_queue );
                 trx_queue.clear();
                 if( new_block.trxs.size() )
                 {
                   chain.push_block( new_block );
                   handle_block( new_block.block_num );
                 }
              }
           }
        } 
        catch ( const fc::exception& e )
        {
           std::cerr<< e.to_detail_string() << "\n";
           exit(-1);
        }
      }
      chain_connection _chain_con;
      bool _chain_connected;
      void chain_connect_loop()
      {
         _chain_connected = false;
         while( true ) //!_quit_promise->ready() )
         {
            //for( auto itr = _config->default_mail_nodes.begin(); itr != _config->default_mail_nodes.end(); ++itr )
            {
                 try {
                    //ilog( "mail connect ${e}", ("e",*itr) );
                    _chain_con.connect(fc::ip::endpoint::from_string("127.0.0.1:4567"));
                  //  _chain_con.set_last_sync_time( _profile->get_last_sync_time() );

                    subscribe_message msg;
                    msg.version        = 0;
                    msg.last_block     = chain.head_block_id();
                    _chain_con.send( mail::message( msg ) );
                    _chain_connected = true;
                    return;
                 } 
                 catch ( const fc::exception& e )
                 {
                    wlog( "${e}", ("e",e.to_detail_string()));
                 }
            }
            fc::usleep( fc::seconds(5) );
         }
      }


      asset get_balance( asset::type u )
      {
          return wallet.get_balance( asset::type(u) );
      }
      void print_balances()
      {
         for( int a = asset::bts; a < asset::count; ++a )
         {
              std::cout << std::string(wallet.get_balance( asset::type(a) )) << "\n";
              /*
              uint64_t amount = wallet.get_balance( asset::type(a) ).amount.high_bits();
              uint64_t fraction = amount % COIN;
              auto fract_str = fc::to_string(static_cast<uint64_t>(fraction+COIN)).substr(1);
              std::cout << (amount/COIN) <<"."<< fract_str << " " << fc::variant(asset::type(a)).as_string() << "\n";
              */
         }
         std::cout<<"\n Margin Positions\n";
         for( int a = asset::bts+1; a < asset::count; ++a )
         {
              asset collat;
              asset due  = wallet.get_margin( asset::type(a), collat );
              uint64_t amount = due.amount.high_bits();
              uint64_t fraction = amount % COIN;
              auto fract_str = fc::to_string(static_cast<uint64_t>(fraction+COIN)).substr(1);
              auto total_collat = collat;
              collat.amount *= 3;
              collat.amount /= 4;
              std::cout << (amount/COIN) <<"."<< fract_str << " " << fc::variant(asset::type(a)).as_string();
              std::cout << "  total collateral: " << std::string( total_collat );
              std::cout << "  avg call price: " << std::string( due/collat );
              std::cout <<"\n";
         }
         wallet.dump();
      }
      void print_market( const std::string& quote, const std::string& base, uint32_t lines = 20 )
      {
         asset::type bunit = fc::variant(base).as<asset::type>();
         asset::type qunit = fc::variant(quote).as<asset::type>();
         if( bunit > qunit ) std::swap( bunit, qunit );

         auto mark = chain.get_market( qunit, bunit );

         std::cout << std::setw( 45 ) << ("      BIDS             ") << "  |";
         std::cout << std::setw( 45 ) << ("      ASKS             ") << "  |";
     //    std::cout << std::setw( 36 ) << ("     SHORTS ("+quote+")        ");
     //    std::cout << std::setw( 36 ) << "     MARGIN     ";
         std::cout << "\n-----------------------------------------------|-----------------------------------------------|\n";
         for( uint32_t i = 0; i < lines; ++i )
         {
            bool end = true;
            if( mark.bids.size() > i ) 
            {
                int bid_index = mark.bids.size() - 1 - i;
                std::cout << std::setw(20) << std::string(asset( mark.bids[bid_index].amount, qunit)*mark.bids[bid_index].bid_price );
                if( !mark.bids[bid_index].is_short )
                {
                   std::cout << " " << std::setw(25) << std::string(mark.bids[bid_index].bid_price) <<" |";
                }
                else
                {
                   std::cout << " " << std::setw(25) << ("-"+std::string(mark.bids[bid_index].bid_price)) <<" |";
                }
                end = false;
            }
            else
            {
                std::cout<< std::setw( 45 ) << " " << "  |";
            }
            if( mark.asks.size() > i )
            {
                std::cout << std::setw(20) << std::string(asset( mark.asks[i].amount,bunit ) );
                std::cout << std::setw(25) << std::string(mark.asks[i].ask_price) <<"  |";
                end = false;
            }
            else
            {
                std::cout<< std::setw( 45 ) << " " << "  |";
            }
            /*
            if( mark.shorts.size() > i )
            {
                std::cout << std::setw(12) << to_balance( mark.shorts[i].amount ) << " " << std::setw(12) << std::string(mark.shorts[i].short_price) <<" |  ";
                end = false;
            }
            else
            {
                std::cout<< std::setw( 37 ) << " " << "|";
            }
            if( mark.margins.size() > i )
            {
                end = false;
            }
            else
            {
                std::cout<< std::setw( 37 ) << " " << "|";
            }
            */
            std::cout <<"\n";

            if( end ) break;
         }
      }

      void print_new_address()
      {
         std::cout<< std::string(wallet.get_new_address()) <<"\n";
      }

      void transfer( double amnt, std::string u, std::string addr )
      {
         asset::type unit = fc::variant(u).as<asset::type>();
         auto trx = wallet.transfer( asset(amnt,unit), addr );
         ilog( "${trx}", ("trx",trx) );
         trx_queue.push_back(trx);
      }
      void short_sell( asset amnt, price p ) //double amnt, std::string u, double sellprice )
      {
         auto trx = wallet.short_sell( amnt, p ); //bts::blockchain::price( sellprice, asset::bts, unit ) );
         std::cout<<"trx id: "<< std::string(trx.id()) <<"\n";
         ilog( "${trx}", ("trx",trx) );
         trx_queue.push_back(trx);
      }

      void buy( asset amount, price pr )
      {
         auto trx = wallet.bid( amount, pr );
         ilog( "${trx}", ("trx",trx) );
         trx_queue.push_back(trx);
      }
      void sell( asset amount, price pr ) //double amnt, std::string u, double buyprice, std::string base )
      {
         auto trx = wallet.bid( amount, pr );
         ilog( "${trx}", ("trx",trx) );
         trx_queue.push_back(trx);
      }


      void dump_chain_html( std::string name )
      {
        std::ofstream html( name.c_str() );
        for( uint32_t i = 0; i <= chain.head_block_num(); ++i )
        {
           auto b = chain.fetch_trx_block(i);
           html << bts::blockchain::pretty_print( b, chain );
        }
      }
      void dump_chain_json( std::string name )
      {
          std::ofstream html( name.c_str() );
          html <<"{\n";
          for( uint32_t i = 0; i <= chain.head_block_num(); ++i )
          {
             auto b = chain.fetch_trx_block(i);
             html << fc::json::to_pretty_string( b );
             if( i != chain.head_block_num() ) html << ",\n";
          }
          html <<"}\n";
      }
      void cover( double amnt, std::string u )
      {
         asset::type unit = fc::variant(u).as<asset::type>();
         auto trx = wallet.cover( asset( amnt, unit ) );
         trx_queue.push_back(trx);
      }

      void print_open_orders( asset::type a )
      {
      }

      void cancel_open_bid( std::string h, uint32_t idx )
      { 
         auto trx = wallet.cancel_bid( output_reference(fc::uint160(h), idx) );
         trx_queue.push_back(trx);
      }

      std::vector<signed_transaction>   trx_queue;

      bts::blockchain::blockchain_db    chain;
      bts::blockchain::wallet           wallet;
      fc::future<void>                  sim_loop_complete;
      fc::future<void>                  chain_connect_loop_complete;
};

void process_commands( fc::thread* main_thread, std::shared_ptr<client> c )
{
   try {
      std::string line;
      std::cout<<">>> ";
      std::getline( std::cin, line );
      while( std::cin.good() )
      {
         try {
         std::stringstream ss(line);
         std::string command;
         ss >> command;
   
         if( command == "h" || command == "help" )
         {
             std::cout<<"Commands:\n";
             std::cout<<" quit\n";
             std::cout<<" balance  -  print the wallet balances\n";
             std::cout<<" newaddr  -  print a new wallet address\n";
             std::cout<<" transfer AMOUNT UNIT to ADDRESS  \n";
             std::cout<<" buy AMOUNT UNIT at PRICE BASE  \n";
             std::cout<<" sell AMOUNT UNIT at PRICE BASE  \n";
             std::cout<<" short AMOUNT UNIT at PRICE (UNIT/bts) \n";
             std::cout<<" cover AMOUNT UNIT  \n";
             std::cout<<" add margin AMOUNT bts to UNIT  \n";
             std::cout<<" cancel ID IDX  \n";
             std::cout<<" html FILE\n";
             std::cout<<" json FILE\n";
             std::cout<<" show orders QUOTE BASE  \n";
         }
         else if( command == "html" )
         {
            std::string file;
            ss >> file;
            main_thread->async( [=](){ c->dump_chain_html(file); } ).wait();
         }
         else if( command == "json" )
         {
            std::string file;
            ss >> file;
            main_thread->async( [=](){ c->dump_chain_json(file); } ).wait();
         }
         else if( command == "c" || command == "cancel" )
         {
            std::string id;
            uint32_t idx;
            ss >> id >> idx;
            main_thread->async( [=](){ c->cancel_open_bid(id,idx); } ).wait();
         }
         else if( command == "q" || command == "quit" )
         {
            main_thread->async( [=](){ c->wallet.save();} ).wait();
            return;
         }
         else if( command == "b" || command == "balance" )
         { 
            main_thread->async( [=](){ c->print_balances(); } ).wait();
         }
         else if( command == "n" || command == "newaddr"  )
         {
            main_thread->async( [=](){ c->print_new_address(); } ).wait();
         }
         else if( command == "t" || command == "transfer" )
         {
            double amount;
            std::string unit;
            std::string to;
            std::string addr;
            ss >> amount >> unit >> to >> addr;
            main_thread->async( [=](){ c->transfer(amount,unit,addr); } ).wait();
         }
         else if( command == "buy" )
         {
            std::string base_str,at;
            double      amount;
            double      quote_price;
            ss >> amount >> base_str;
            asset::type base_unit = fc::variant(base_str).as<asset::type>();
            asset       amnt = asset(amount,base_unit);

            std::cout<< "price per "<<base_str<<" (ie: 1 usd): ";
            std::getline( std::cin, line );
            std::stringstream pline(line);
            std::string quote_str;
            pline >> quote_price >> quote_str;
            asset::type quote_unit = fc::variant(quote_str).as<asset::type>();

            bts::blockchain::price pr =  asset( quote_price, quote_unit ) / asset( 1.0, base_unit );
            auto required_input = amnt * pr;
            auto curr_bal = main_thread->async( [=](){ return c->get_balance(required_input.unit); } ).wait();

            std::cout<<"current balance: "<< to_balance( curr_bal.amount.high_bits() ) <<" "<<fc::variant(required_input.unit).as_string()<<"\n"; 
            std::cout<<"total price: "<< to_balance(required_input.amount.high_bits()) <<" "<<fc::variant(required_input.unit).as_string()<<"\n"; 

            if( required_input > curr_bal )
            {
                std::cout<<"Insufficient Funds\n";
            }
            else
            {
                std::cout<<"submit order? (y|n): ";
                std::getline( std::cin, line );
                if( line == "yes" || line == "y" )
                {
                    main_thread->async( [=](){ c->buy(required_input,pr); } ).wait();
                    std::cout<<"order submitted\n";
                }
                else
                {
                    std::cout<<"order canceled\n";
                }
            }
         }
         else if( command == "sell" )
         {
            std::string base_unit_str;
            double      base_amount;
            ss >> base_amount >> base_unit_str;
            asset::type base_unit = fc::variant(base_unit_str).as<asset::type>();
            asset       base_amnt = asset(base_amount,base_unit);

            auto cur_bal = main_thread->async( [=](){ return c->get_balance(base_unit); } ).wait();
            std::cout<<"current balance: "<< std::string(cur_bal) <<"\n"; //to_balance( cur_bal.amount.high_bits() ) <<" "<<unit_str<<"\n"; 
            if( cur_bal < base_amnt )
            {
                std::cout<<"Insufficient Funds\n";
            }
            else
            {
               // TODO: get current bid/ask for all other assets as reference points

               std::cout<< "price per "<<base_unit_str<<" (ie: 1 usd): ";
               std::getline( std::cin, line );
               std::stringstream pline(line);
               double   quote_price;
               std::string quote_unit_str;
               pline >> quote_price >> quote_unit_str;

               asset::type quote_unit = fc::variant(quote_unit_str).as<asset::type>();

               auto quote_asset = asset( quote_price, quote_unit ) / asset( 1.0, base_unit );

               if( quote_unit == base_unit )
               {
                  std::cout<<"Attempt to sell for same asset\n";
               }
               else
               {
                  std::cout<<"Expected Proceeds: "<< std::string( base_amnt*quote_asset) <<"\n";//to_balance( (amnt*quote_price).amount.high_bits() ) <<" "<<quote_unit_str<<"\n";
                  std::cout<<"submit order? (y|n): ";
                  std::getline( std::cin, line );
                  if( line == "yes" || line == "y" )
                  {
                      main_thread->async( [=](){ c->sell(base_amnt,quote_asset); } ).wait();
                      std::cout<<"order submitted\n";
                  }
                  else
                  {
                      std::cout<<"order canceled\n";
                  }
               }
            }
         }
         else if( command == "short" )
         {
            std::string quote_unit_str;
            double      quote_amount;
            ss >> quote_amount >> quote_unit_str;
            asset::type quote_unit = fc::variant(quote_unit_str).as<asset::type>();
            asset       quote_amnt = asset(quote_amount,quote_unit);

            std::cout<< "price ("<<quote_unit_str<<"/bts): ";
            std::getline( std::cin, line );
            std::stringstream pline(line);
            double   quote_price;
            pline >> quote_price;
            bts::blockchain::price short_price = asset( quote_price, quote_unit ) / asset( 1.0, asset::bts ); //( priced, unit, asset::bts ); //asset::bts, unit );
            auto required_input = quote_amnt * short_price;

            std::cout<<"current balance: "<<  std::string(main_thread->async( [=](){ return c->get_balance(asset::bts); } ).wait())<<"\n"; 
            std::cout<<"required collateral: "<< std::string(required_input) <<"\n"; 
            std::cout<<"submit order? (y|n): ";
            std::getline( std::cin, line );
            if( line == "yes" || line == "y" )
            {
                main_thread->async( [=](){ c->short_sell(quote_amnt,short_price); } ).wait();
                std::cout<<"order submitted\n";
            }
            else
            {
                std::cout<<"order canceled\n";
            }
         }
         else if( command == "cover" )
         {
            double amount;
            std::string unit;
            ss >> amount >> unit;
            main_thread->async( [=](){ c->cover(amount,unit); } ).wait();
         }
         else if( command == "market" )
         {
            std::string quote_unit;
            std::string base_unit;
            ss >> quote_unit >> base_unit;
            main_thread->async( [=](){ c->print_market(quote_unit,base_unit); } ).wait();
         }
         else if( command == "add" )
         {
         }
         else if( command == "show" )
         {
         }
         std::cout<<">>> ";
         } 
         catch( const fc::exception& e) 
         {
             std::cerr<<e.to_detail_string()<<"\n";
         }
         std::getline( std::cin, line );
      }
   } 
   catch ( const fc::exception& e )
   {
      std::cerr<< e.to_detail_string() <<"\n";
      exit(1);
   }
}


int main( int argc, char** argv )
{ 
   auto main_thread = &fc::thread::current();

   fc::file_appender::config ac;
   ac.filename = "log.txt";
   ac.truncate = false;
   ac.flush    = true;
   fc::logging_config cfg;

   cfg.appenders.push_back(fc::appender_config( "default", "file", fc::variant(ac)));

   fc::logger_config dlc;
   dlc.level = fc::log_level::debug;
   dlc.name = "default";
   dlc.appenders.push_back("default");
   cfg.loggers.push_back(dlc);
   fc::configure_logging( cfg );

   try {
     auto  bts_client = std::make_shared<client>();
     bts_client->open( "datadir" );
     
     fc::thread  read_thread;
     read_thread.async( [=](){ process_commands( main_thread, bts_client ); } ).wait();
   } 
   catch ( const fc::exception& e )
   {
      std::cerr<< e.to_string() << "\n";
      return -1;
   }
   return 0;
}
