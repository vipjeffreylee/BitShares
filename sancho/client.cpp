#include <iostream>
#include <bts/application.hpp>
#include <fc/filesystem.hpp>
#include <fc/io/json.hpp>
#include <fc/log/logger.hpp>
#include <fc/io/fstream.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/rpc/json_connection.hpp>
#include <fc/interprocess/signals.hpp>
#include <iostream>

int main( int argc, char** argv )
{
   try 
   {
      if( argc == 1 )
      {
         std::cout<<"usage:\n    "<<argv[0]<<" [CONFIG] COMMAND ARG1 ARG2 ... ARGN \n";
         return -1;
      }

      int command_index = 1;
      std::string config_file = "config.json";
      if( fc::exists( argv[1] ) )
      {
         config_file = argv[1];
         command_index = 2;
      }

      if( !fc::exists( config_file ) )
      {
         std::cerr<<"Unable to open "<<fc::absolute(config_file).generic_string()<<"\n";
         return -1;
      }

      fc::variants args;
      for( int arg = command_index+1; arg < argc; ++arg )
      {
          args.push_back( fc::json::from_string( argv[arg] ) );
      }

      auto app_config = fc::json::from_file( config_file ).as<bts::application_config>();

      fc::tcp_socket_ptr sock = std::make_shared<fc::tcp_socket>();
      sock->connect_to( fc::ip::endpoint( fc::ip::address("127.0.0.1"), app_config.rpc_config.port ) );
      fc::buffered_istream_ptr isock = std::make_shared<fc::buffered_istream>(sock);
      fc::buffered_ostream_ptr osock = std::make_shared<fc::buffered_ostream>(sock);
      fc::rpc::json_connection_ptr con = std::make_shared<fc::rpc::json_connection>( isock, osock );
      con->exec();

      auto result = con->async_call( "login", app_config.rpc_config.user, app_config.rpc_config.pass ).wait();
      ilog( "login result: ${r}", ("r",result) );
      if( !result.as<bool>() )
      {
          std::cerr<<"invalid login\n";
          return -1;
      }
      result = con->async_call( argv[command_index], args ).wait();
      ilog( "command result: ${r}", ("r",result) );
   } 
   catch ( const fc::exception& e )
   {
       std::cerr << e.to_detail_string() << std::endl;
       return -1;
   }
   std::cout<<"Exited Normally\n";
}
