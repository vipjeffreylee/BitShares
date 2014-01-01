#include <bts/db/level_map.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/network/tcp_socket.hpp>
#include <fc/rpc/json_connection.hpp>
#include <fc/thread/thread.hpp>
#include <fc/filesystem.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/io/raw.hpp>
#include <fc/exception/exception.hpp>

#include <fc/log/logger.hpp>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iostream>

struct record
{
    record():points(0){}
    record( std::string k, double p )
    :key(k),points(p){}

    std::string key;
    double    points;
    std::string pub_key;
};

FC_REFLECT( record, (key)(points)(pub_key) )

int main( int argc, char** argv )
{
   try {
         fc::tcp_server                           _tcp_serv;
         bts::db::level_map<std::string,record>   _known_names;
         _known_names.open( "reg_db" );
         
         // TODO: import CSV list.
         if( argc == 2 )
         {
            FC_ASSERT( fc::exists(argv[1]) );
            std::ifstream in(argv[1]);
            std::string line;
            std::getline(in, line);
            while( in.good() )
            {
               std::stringstream ss(line);
               std::string name;
               std::getline( ss, name, ',' );
               std::string key;
               std::getline( ss, key, ',' );
               std::string points;
               std::getline( ss, points, ',' );
               auto itr = _known_names.find( name );
               if( !itr.valid() )
               {
                  _known_names.store( name, record( key, fc::variant(points).as_double() ) );
               }
               std::getline(in, line);
            }
         }

         //fc::future<void>    _accept_loop_complete = fc::async( [&]() {
             while( true ) //!_accept_loop_complete.canceled() )
             {
                fc::tcp_socket_ptr sock = std::make_shared<fc::tcp_socket>();
                try 
                {
                  _tcp_serv.accept( *sock );
                }
                catch ( const fc::exception& e )
                {
                  elog( "fatal: error opening socket for rpc connection: ${e}", ("e", e.to_detail_string() ) );
                  //exit(1);
                }
             
                auto buf_istream = std::make_shared<fc::buffered_istream>( sock );
                auto buf_ostream = std::make_shared<fc::buffered_ostream>( sock );
             
                auto json_con = std::make_shared<fc::rpc::json_connection>( std::move(buf_istream), std::move(buf_ostream) );
                json_con->add_method( "register_key", [&]( const fc::variants& params ) -> fc::variant 
                {
                    FC_ASSERT( params.size() == 3 );
                    auto rec = _known_names.fetch( params[0].as_string() );
                    FC_ASSERT( rec.key == params[1].as_string() );
                    FC_ASSERT( rec.pub_key.size() == 0 || rec.pub_key == params[2].as_string() );
                    rec.pub_key = params[2].as_string();
                    _known_names.store( params[0].as_string(), rec );
                    return fc::variant( rec );
                });

                fc::async( [json_con]{ json_con->exec().wait(); } );
              }
        // }
        // );


         //_accept_loop_complete.wait();
         return 0;
   } 
   catch ( fc::exception& e )
   {
      elog( "${e}", ("e",e.to_detail_string() ) );
   }
}

