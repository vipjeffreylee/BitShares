#pragma once
#include <fc/network/tcp_socket.hpp>
#include <fc/thread/mutex.hpp>
#include <fc/thread/scoped_lock.hpp>
#include "messages.hpp"

class connection
{
   public:
      connection(){}
      connection( const fc::tcp_socket_ptr& s )
      :sock(s){}

      ~connection()
      {
          if( sock ) sock->close();
      }

      void send( const message& m )
      {
          auto data = fc::raw::pack(m);
          fc::scoped_lock<fc::mutex> lock(write_mutex);
          sock->write( data.data(), data.size() );
      }

      message recv()
      {
          message msg;
          fc::raw::unpack( *sock, msg );
          return msg;
      }
    
      fc::mutex          write_mutex;
      fc::tcp_socket_ptr sock;
};
