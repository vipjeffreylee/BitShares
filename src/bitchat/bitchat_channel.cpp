#include <bts/bitchat/bitchat_channel.hpp>
#include <bts/bitchat/bitchat_messages.hpp>
#include <bts/bitchat/bitchat_private_message.hpp>
#include <bts/bitchat/bitchat_message_cache.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/thread/thread.hpp>
#include <fc/log/logger.hpp>
#include <unordered_map>
#include <map>

namespace bts { namespace bitchat {

  using network::channel_id;
  using network::connection_ptr;

  namespace detail 
  {
     class chan_data : public network::channel_data
     {
        public:
          chan_data():check_cache(true){}
          std::unordered_set<fc::uint128> known_inv;
          bool                            check_cache;
     };


     class channel_impl : public bts::network::channel 
     {
        public:
          channel_impl()
          :target_difficulty(1){}

          channel_id                                         chan_id;
          channel_delegate*                                  del;
          peer::peer_channel_ptr                             peers;
                                                             
          message_cache                                      _message_cache;
          uint64_t                                           target_difficulty;

          std::map<fc::time_point, fc::uint128>              msg_time_index;
          std::unordered_map<fc::uint128,encrypted_message>  priv_msgs;

          /// messages that we have recieved inv for, but have not requested the data for
          std::unordered_set<fc::uint128>                    unknown_msgs; 
          std::unordered_map<fc::uint128,fc::time_point>     requested_msgs; // messages that we have requested but not yet received
                                                             
          std::vector<fc::uint128>                           new_msgs;  // messages received since last inv broadcast
                                                             
          fc::future<void>                                   fetch_loop_complete;

          /**
           *  Get or create the bitchat channel data for this connection and return
           *  a reference to the result.
           */
          chan_data& get_channel_data( const connection_ptr& c )
          {
              auto cd = c->get_channel_data( chan_id );
              if( !cd )
              {
                 cd = std::make_shared<chan_data>();
                 c->set_channel_data( chan_id, cd );
              }
              chan_data& cdat = cd->as<chan_data>();
              return cdat;
          }

          virtual void handle_subscribe( const connection_ptr& c )
          {
              chan_data& cdat = get_channel_data(c);
          }
          virtual void handle_unsubscribe( const connection_ptr& c )
          {
              c->set_channel_data( chan_id, nullptr );
          }
          virtual void handle_message( const connection_ptr& c, const bts::network::message& m )
          {
              chan_data& cdat = get_channel_data(c);

              /** when we first connect to a new node, download the message cache... */
              if( cdat.check_cache )
              {
                 cdat.check_cache = false;
                 c->send( network::message( get_cache_inv_message( _message_cache.last_message_timestamp(), 
                                                               fc::time_point::now() ), chan_id )  ); 
              }

              ilog( "${msg_type}", ("msg_type", (bitchat::message_type)m.msg_type ) );
              switch( (bitchat::message_type)m.msg_type )
              {
                  case inv_msg:
                     handle_inv( c, cdat, m.as<inv_message>()  );
                     break;
                  case get_inv_msg:
                     handle_get_inv( c, cdat, m.as<get_inv_message>()  );
                     break;
                  case get_priv_msg:
                     handle_get_priv( c, cdat, m.as<get_priv_message>()  );
                     break;
                  case encrypted_msg:
                     handle_priv_msg( c, cdat, m.as<encrypted_message>()  );
                     break;
                  case cache_inv_msg:
                     handle_cache_inv( c, cdat, m.as<cache_inv_message>() );
                     break;
                  case get_cache_priv_msg:
                     handle_get_cache_priv_msg( c, cdat, m.as<get_cache_priv_message>() );
                     break;
                  case get_cache_inv_msg:
                     handle_get_cache_inv( c, cdat, m.as<get_cache_inv_message>() );
                     break;
                  default:
                     // TODO: figure out how to document this / punish the connection that sent us this 
                     // message.
                     wlog( "unknown bitchat message type ${t}", ("t",uint64_t(m.msg_type)) );
              }
          }

          void handle_cache_inv( const connection_ptr& c, chan_data& cdat, cache_inv_message msg )
          { try {
               ilog( "${msg}", ("msg",msg) );
               c->send( network::message( get_cache_priv_message( msg.items ) ) );
          } FC_RETHROW_EXCEPTIONS( warn, "", ("msg",msg) ) }

          void handle_get_cache_priv_msg(const connection_ptr& c, chan_data& cdat, get_cache_priv_message msg )
          { try {
              ilog( "${msg}", ("msg",msg) );
              for( auto itr = msg.items.begin(); itr != msg.items.end(); ++itr )
              {
                 auto msg = _message_cache.fetch( *itr );
                 c->send( network::message( msg, chan_id ) );
              }
          } FC_RETHROW_EXCEPTIONS( warn, "", ("msg",msg) ) }

          void handle_get_cache_inv( const connection_ptr& c, chan_data& cdat, get_cache_inv_message msg)
          { try { 
              ilog( "${msg}", ("msg",msg) );
              // TODO: rate limit this message from c
              cache_inv_message reply;
              reply.items = _message_cache.get_inventory( msg.start_time, msg.end_time );
              c->send( network::message( reply, chan_id ) ); 
          } FC_RETHROW_EXCEPTIONS( warn, "", ("msg",msg) ) }

          void fetch_loop()
          {
             try {
                while( !fetch_loop_complete.canceled() )
                {
                   broadcast_inv();
                   if( unknown_msgs.size()  )
                   {
                      auto cons = peers->get_connections( chan_id );
                      // copy so we don't hold shared state in the iterators
                      // while we iterate and send fetch requests
                      auto tmp = unknown_msgs; 
                      for( auto itr = tmp.begin(); itr != tmp.end(); ++itr )
                      {
                          fetch_from_best_connection( cons, *itr );
                      }
                   }
                   /* By using a random sleep we give other peers the oppotunity to find
                    * out about messages before we pick who to fetch from.
                    */
                   fc::usleep( fc::microseconds( (rand() % 20000) + 100) ); // note: usleep(0) sleeps forever... perhaps a bug?
                }
             } 
             catch ( const fc::exception& e )
             {
               wlog( "${e}", ("e", e.to_detail_string()) );
             }
          }

          /**
           *   For any given message id, there are many potential hosts from which it could be fetched.  We
           *   want to distribute the load across all hosts equally and therefore, the best one to fetch from
           *   is the host that we have fetched the least from and that has fetched the most from us.
           *
           */
          void fetch_from_best_connection( const std::vector<connection_ptr>& cons, const fc::uint128& id )
          {
             // if request is made, move id from unknown_msgs to requested_msgs 
             // TODO: update this algorithm to be something better. 
             for( uint32_t i = 0; i < cons.size(); ++i )
             {
                 chan_data& cd = get_channel_data(cons[i]); 
                 if( cd.known_inv.find( id ) !=  cd.known_inv.end() )
                 {
                    requested_msgs[id] = fc::time_point::now();
                    unknown_msgs.erase(id);
                    cons[i]->send( network::message( get_priv_message( id ), chan_id ) );
                    return;
                 }
             }
          }

          /**
           *  Send any new inventory items that we have received since the last
           *  broadcast to all connections that do not know about the inv item.
           */
          void broadcast_inv()
          {
              if( new_msgs.size() )
              {
                auto cons = peers->get_connections( chan_id );
                for( auto c = cons.begin(); c != cons.end(); ++c )
                {
                  inv_message msg;

                  chan_data& cd = get_channel_data( *c );
                  for( uint32_t i = 0; i < new_msgs.size(); ++i )
                  {
                     if( cd.known_inv.insert( new_msgs[i] ).second )
                     {
                        msg.items.push_back( new_msgs[i] );
                     }
                  }

                  if( msg.items.size() )
                  {
                    (*c)->send( network::message(msg,chan_id) );
                  }
                }
                new_msgs.clear();
              }
          }


          /**
           *  Note that c knows about items and add any unknown items to our queue 
           */
          void handle_inv( const connection_ptr& c, chan_data& cd, const inv_message& msg )
          {
              ilog( "inv: ${msg}", ("msg",msg) );
              for( auto itr = msg.items.begin(); itr != msg.items.end(); ++itr )
              {
                 cd.known_inv.insert( *itr );
                 if( priv_msgs.find( *itr ) == priv_msgs.end() )
                 {
                    unknown_msgs.insert( *itr );
                 }
              }
          }

          /**
           *  Send all inventory items that are not known to c and are dated 'after'
           */
          void handle_get_inv( const connection_ptr& c, chan_data& cd, const get_inv_message& msg )
          {
             ilog( "${msg}", ("msg",msg) );
             inv_message reply;
             for( auto itr = msg_time_index.lower_bound( fc::time_point(msg.after) ); itr != msg_time_index.end(); ++itr )
             {
                if( cd.known_inv.insert( itr->second ).second )
                {
                   reply.items.push_back( itr->second );
                   cd.known_inv.insert( itr->second );
                }
             }
             c->send( network::message( reply, chan_id ) );
          }
            

          void handle_get_priv( const connection_ptr& c, chan_data& cd, const get_priv_message& msg )
          {
             ilog( "${msg}", ("msg",msg) );
             // TODO: throttle the rate at which get_priv may be called for a given connection
             for( auto itr = msg.items.begin(); itr != msg.items.end(); ++itr )
             {
                auto m = priv_msgs.find( *itr );
                if( m != priv_msgs.end() )
                {
                   c->send( network::message( m->second, chan_id ) );
                   cd.known_inv.insert( *itr ); 
                }
             }
          }

          void handle_priv_msg( const connection_ptr& c, chan_data& cd, encrypted_message&& msg )
          {
             ilog( "${msg}", ("msg",msg) );
              auto mid = msg.id();
              // TODO: verify that we requested this message
              
              FC_ASSERT( msg.validate_proof() );
              FC_ASSERT( msg.difficulty() >= target_difficulty );

              // track messages that I've requested and make sure that no one sends us a msg we haven't requested
              if( priv_msgs.find(mid) == priv_msgs.end() )
              {

                 // must not be more than 30 minutes old
                 if( (fc::time_point::now() - msg.timestamp) < fc::seconds(60*30) &&
                      msg.timestamp < (fc::time_point::now()+fc::seconds(60*5)) ) 
                 {
                    new_msgs.push_back( mid ); // store so message can be broadcast... but only if time is right
                    msg_time_index[fc::time_point::now()] = mid;
                 }   

                 const encrypted_message& m = (priv_msgs[mid] = std::move(msg));

                 _message_cache.cache( m );

                 del->handle_message( m, chan_id );
              }
              else
              {
                 wlog( "duplicate message received" );
              }
          }
     };


  } // namespace detail
  
  channel::channel( const bts::peer::peer_channel_ptr& p, const channel_id& c, channel_delegate* d )
  :my( std::make_shared<detail::channel_impl>() )
  {
     assert( d != nullptr );
     my->peers = p;
     my->del = d;
     my->chan_id = c;

     my->peers->subscribe_to_channel( c, my );

     my->fetch_loop_complete = fc::async( [=](){ my->fetch_loop(); } );
  }

  channel::~channel()
  {
     my->peers->unsubscribe_from_channel( my->chan_id );
     my->del = nullptr;
     try {
        my->fetch_loop_complete.cancel();
        my->fetch_loop_complete.wait();
     } 
     catch ( ... ) 
     {
        wlog( "unexpected exception ${e}", ("e", fc::except_str()));
     }
  }


  channel_id channel::get_id()const { return my->chan_id; }

  /**
   *  Places the message into the queue of received messages as if
   *  we had received it from another node.  The next time my->broadcast_inv()
   *  is called it will include this message in the inventory list and
   *  it will appear indistigusable to other nodes.
   */
  void channel::broadcast( encrypted_message&& m )
  {
      //TODO: make 5 minute a constant in bts/config.hpp
      FC_ASSERT( fc::time_point::now() - m.timestamp  <  fc::seconds(60*5) );
      FC_ASSERT( fc::time_point(m.timestamp) <= fc::time_point::now() );

      auto id = m.id();
      my->priv_msgs[ id ] = std::move(m);
      my->msg_time_index[ m.timestamp ] = id;
      my->new_msgs.push_back(id);
  }

  void channel::configure( const channel_config& conf )
  {
      auto dir = conf.data_dir / ("cache_chan_" + fc::variant(my->chan_id.id()).as_string());
      fc::create_directories( dir );
      my->_message_cache.open( dir );
  }


} } // namespace bts::bitchat
