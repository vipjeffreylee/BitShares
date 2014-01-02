/** 
 *  This executable implements a simple mail server that performs the job of recording and
 *  rebroadcasting messages as they are sent.  The server uses Proof of Work to limit 
 *  SPAM and control the data rate.  The target is to allow only 200 MB per day to be 
 *  broadcast.  This is 100 MB per user per day maximum that will stream through the
 *  server and averaging about 1.5 kb/sec per user.
 *
 *  When a user connects they receive a status message from the server that includes:
 *     Version, Server Time, Difficulty, Mirrors, Message Count
 *
 *  Eventually a user can create individual mailboxes.
 */
#include <mail/mail_server.hpp>
#include <fc/thread/thread.hpp>
#include <fc/log/logger.hpp>

int main( int argc, char** argv )
{
   try {
       mail::server mserv;
       mail::server::config cfg;
       cfg.port = 7896;
       mserv.configure(cfg);
       fc::usleep( fc::seconds( 60*60*24*365 ) );
   } 
   catch ( const fc::exception& e )
   {
     elog( "${e}", ("e",e.to_detail_string() ) );
     return -1;
   }
   ilog( "Exiting normally" );
   return 0;
}
