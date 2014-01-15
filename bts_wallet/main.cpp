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

using namespace bts::blockchain;

class client_delegate
{

};


fc::ecc::private_key test_genesis_private_key()
{
    return fc::ecc::private_key::generate_from_seed( fc::sha256::hash( "genesis", 7 ) );
}

bts::blockchain::trx_block create_test_genesis_block()
{
   bts::blockchain::trx_block b;
   b.version      = 0;
   b.prev         = block_id_type();
   b.block_num    = 0;
   b.total_shares = 100*COIN;
   b.timestamp    = fc::time_point::from_iso_string("20131201T054434");

   signed_transaction coinbase;
   coinbase.version = 0;
   //coinbase.valid_after = 0;
   //coinbase.valid_blocks = 0;

   // TODO: init from PTS here...
   coinbase.outputs.push_back( 
      trx_output( claim_by_signature_output( bts::address(test_genesis_private_key().get_public_key()) ), 100*COIN, asset::bts) );

   b.trxs.emplace_back( std::move(coinbase) );
   b.trx_mroot   = b.calculate_merkle_root();

   return b;
}



class client
{
   public:
      void open( const fc::path& datadir )
      {
          chain.open( datadir / "chain" );
          wallet.open( datadir / "wallet.bts" );

          if( chain.head_block_num() == uint32_t(-1) )
          {
              auto genesis = create_test_genesis_block();
              //ilog( "genesis block: \n${s}", ("s", fc::json::to_pretty_string(genesis) ) );
              chain.push_block( genesis );
              wallet.import_key( test_genesis_private_key() );
              wallet.scan_chain( chain, 0 );
              wallet.save();
          }
          wallet.scan_chain( chain );

          sim_loop_complete = fc::async( [this]() { server_sim_loop(); } );
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
              fc::usleep( fc::seconds(5) );

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
      std::string to_balance( uint64_t a )
      {
          uint64_t fraction = a % COIN;
          auto fract_str = fc::to_string(static_cast<uint64_t>(fraction+COIN)).substr(1);
          return fc::to_string( a/COIN ) + "." + fract_str;
      }

      void print_balances()
      {
         for( int a = asset::bts; a < asset::count; ++a )
         {
              uint64_t amount = wallet.get_balance( asset::type(a) ).amount.high_bits();
              uint64_t fraction = amount % COIN;
              auto fract_str = fc::to_string(static_cast<uint64_t>(fraction+COIN)).substr(1);
              std::cout << (amount/COIN) <<"."<< fract_str << " " << fc::variant(asset::type(a)).as_string() << "\n";
         }
         std::cout<<"\n Margin Positions\n";
         for( int a = asset::bts+1; a < asset::count; ++a )
         {
              asset collat;
              asset due  = wallet.get_margin( asset::type(a), collat );
              uint64_t amount = due.amount.high_bits();
              uint64_t fraction = amount % COIN;
              auto fract_str = fc::to_string(static_cast<uint64_t>(fraction+COIN)).substr(1);
              collat.amount *= 3;
              collat.amount /= 4;
              std::cout << (amount/COIN) <<"."<< fract_str << " " << fc::variant(asset::type(a)).as_string() << "  Avg Call Price: ";
              std::cout << std::string( due/collat );
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

         std::cout << std::setw( 36 ) << ("      BIDS  ("+quote+")        ");
         std::cout << std::setw( 36 ) << ("      ASKS  ("+base+")         ");
         std::cout << std::setw( 36 ) << ("     SHORTS ("+quote+")        ");
         std::cout << std::setw( 36 ) << "     MARGIN     ";
         std::cout << "\n--------------------------------------------------------------------------------\n";
         for( uint32_t i = 0; i < lines; ++i )
         {
            bool end = true;
            if( mark.bids.size() > i ) 
            {
                std::cout << std::setw(12) << to_balance(mark.bids[i].amount) << " " << std::setw(12) << std::string(mark.bids[i].bid_price) <<" |  ";
                end = false;
            }
            else
            {
                std::cout<< std::setw( 37 ) << " " << "|";
            }
            if( mark.asks.size() > i )
            {
                std::cout << std::setw(12) << to_balance( mark.asks[i].amount ) << " " << std::setw(12) << std::string(mark.asks[i].ask_price) <<" |  ";
                end = false;
            }
            else
            {
                std::cout<< std::setw( 37 ) << " " << "|";
            }
            if( mark.shorts.size() > i )
            {
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
         uint64_t amount = amnt * COIN;
         asset::type unit = fc::variant(u).as<asset::type>();
         auto trx = wallet.transfer( asset(amount,unit), addr );
         ilog( "${trx}", ("trx",trx) );
         trx_queue.push_back(trx);
      }
      void short_sell( double amnt, std::string u, double sellprice )
      {
         uint64_t amount = amnt * COIN;
         asset::type unit = fc::variant(u).as<asset::type>();
         auto trx = wallet.short_sell( asset(amount,unit), bts::blockchain::price( sellprice, asset::bts, unit ) );
         ilog( "${trx}", ("trx",trx) );
         trx_queue.push_back(trx);
      }

      void buy( double amnt, std::string u, double buyprice, std::string base )
      {
         uint64_t amount = amnt * COIN;
         asset::type unit = fc::variant(u).as<asset::type>();
         asset::type base_unit = fc::variant(base).as<asset::type>();
         auto p = bts::blockchain::price( buyprice, base_unit, unit );
         auto a = asset(amount,unit) * p;
         auto trx = wallet.bid( a, p );
         ilog( "${trx}", ("trx",trx) );
         trx_queue.push_back(trx);
      }
      void sell( double amnt, std::string u, double buyprice, std::string base )
      {
         uint64_t amount = amnt * COIN;
         asset::type unit = fc::variant(u).as<asset::type>();
         asset::type base_unit = fc::variant(base).as<asset::type>();
         auto p = bts::blockchain::price( buyprice, base_unit, unit );
         auto a = asset(amount,unit);
         auto trx = wallet.bid( a, p );
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
         uint64_t amount = amnt * COIN;
         asset::type unit = fc::variant(u).as<asset::type>();
         auto trx = wallet.cover( asset( amount, unit ) );
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
            double amount;
            std::string unit;
            std::string base_unit;
            std::string at;
            std::string addr;
            double buyprice;
            ss >> amount >> unit >> at >> buyprice >> base_unit;
            main_thread->async( [=](){ c->buy(amount,unit,buyprice,base_unit); } ).wait();
         }
         else if( command == "sell" )
         {
            double amount;
            std::string unit;
            std::string base_unit;
            std::string at;
            std::string addr;
            double buyprice;
            ss >> amount >> unit >> at >> buyprice >> base_unit;
            main_thread->async( [=](){ c->sell(amount,unit,buyprice,base_unit); } ).wait();
         }
         else if( command == "short" )
         {
            std::string unit,at;
            double      amount;
            double      price;
            ss >> amount >> unit >> at >> price;

            main_thread->async( [=](){ c->short_sell(amount,unit,price); } ).wait();
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
