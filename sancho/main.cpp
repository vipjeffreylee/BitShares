#include <bts/application.hpp>
#include <fc/filesystem.hpp>
#include <fc/io/json.hpp>
#include <fc/log/logger.hpp>
#include <fc/io/fstream.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/interprocess/signals.hpp>
#include <iostream>

#include <signal.h>

void handle_signal( int sig )
{
    static int count = 0;
    if( count == 0 )
    {
      ++count;
      bts::application::instance()->quit();
    }
    else
    {
        exit(1);
    }
}

bts::application_config load_config( const fc::path& data_dir )
{ try {
     fc::create_directories(data_dir);
     auto config_file  = data_dir / "config.json";
     ilog( "config_file: ${file}", ("file",config_file) );
     if( !fc::exists( config_file ) )
     {
        bts::application_config default_cfg;
        default_cfg.data_dir = data_dir / "data";

        fc::ofstream out( config_file );
        out << fc::json::to_pretty_string( default_cfg );
     }

     auto app_config = fc::json::from_file( config_file ).as<bts::application_config>();
     fc::ofstream out( config_file );
     out << fc::json::to_pretty_string( app_config );
     return app_config;
} FC_RETHROW_EXCEPTIONS( warn, "") }


int main( int argc, char** argv )
{
   try 
   {
      fc::path data_dir = ( argc > 1 ) ? std::string(argv[1]) : ".";

      bts::application_ptr  app = bts::application::instance(); //std::make_shared<bts::application>();

      app->configure( load_config( data_dir ) );


      signal( SIGINT, handle_signal );
      #endif
      app->wait_until_quit();
   } 
   catch ( const fc::exception& e )
   {
       std::cerr << e.to_detail_string() << std::endl;
       return -1;
   }
   std::cout<<"Exited Normally\n";
   return 0;
}
