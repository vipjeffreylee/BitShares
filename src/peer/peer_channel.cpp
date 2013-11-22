#include <bts/peer/peer_messages.hpp>
#include <bts/peer/peer_channel.hpp>
#include <bts/network/broadcast_manager.hpp>
#include <bts/db/level_map.hpp>
#include <fc/log/logger.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/thread/thread.hpp>
#include <unordered_map>
#include <algorithm>

namespace bts { namespace peer {
   using namespace bts::network;

   namespace detail 
   {

      class peer_data : public channel_data
      {
         public:
           peer_data():requested_hosts(false){}
           // data stored with the connection
           std::unordered_set<uint32_t> subscribed_channels;
           fc::ip::endpoint             public_contact; // used for reverse connection
           fc::optional<config_msg>     peer_config;
           bool                         requested_hosts;

           broadcast_manager<uint64_t,announce_msg>::channel_data  announce_messages;
      };

      class channel_connection_index
      {
         public:
             void add_connection( connection* c )
             {
                connections.push_back(c);
             }
             void remove_connection( connection* c )
             {
                auto p = std::find( connections.begin(), connections.end(), c );
                assert( p != connections.end() ); // invariant, crash in debug
                if( p != connections.end() ) // don't crash in release
                {
                   connections.erase(p);
                }
                else
                {
                   wlog("invariant not maintained" );
                }
             }
             std::vector<connection_ptr> get_connections()const
             {
                std::vector<connection_ptr> cons;
                cons.reserve(connections.size());
                for( auto itr = connections.begin(); itr != connections.end(); ++itr )
                {
                  cons.push_back( (*itr)->shared_from_this() );
                }
                return cons;
             }
         private:
             std::vector<connection*> connections;
      };


      class peer_channel_impl : public channel, public server_delegate
      {
         public:
           server_ptr netw;
           bts::network::channel_id                               _chan_id;
           
           /** maps a channel ID to all connections subscribed to that channel */
           std::unordered_map<uint32_t,channel_connection_index>  cons_by_channel;
           std::unordered_set<uint32_t>                           subscribed_channels;

           /**
            *  Store all hosts we know about sorted by time since we last heard about them.
            *  This list is provided to new nodes when they connect.  Limit to 1000 nodes.
            */
           std::vector<host>                                      recent_hosts;

           bts::db::level_map<uint64_t,announce_msg>              known_hosts;

           announce_msg                                           last_announce;
           fc::thread                                             announce_miner_thread;
           fc::future<void>                                       announce_mining_complete;
           
           broadcast_manager<uint64_t,announce_msg>               announce_broadcasts;

           void broadcast_inv()
           { try {
              if( announce_broadcasts.has_new_since_broadcast() )
              {
                 auto con_chan_itr = cons_by_channel.find( _chan_id.id() );
                 auto cons = con_chan_itr->second.get_connections();
                 for( auto itr = cons.begin(); itr != cons.end(); ++itr )
                 {
                    announce_inv_msg inv_msg;
                    peer_data& con_data = get_channel_data(*itr);
                    
                    inv_msg.announce_msgs = announce_broadcasts.get_inventory( 
                                                               con_data.announce_messages );
                    if( inv_msg.announce_msgs.size() != 0 )
                    {
                       (*itr)->send( network::message( inv_msg, _chan_id ) );
                       con_data.announce_messages.update_known( inv_msg.announce_msgs );
                    }
                 }
                 // TODO: send() may yield and thus there may in fact be new since
                 // the last broadcast... should I copy state before starting loop 
                 // above?
                 announce_broadcasts.set_new_since_broadcast(false);
              }
           } FC_RETHROW_EXCEPTIONS( warn, "error broadcasting announcement inventory" ) }


           /**
            *  Stores the host in the known host list if it has a valid IP address and
            *  a recent time stamp.
            */
           bool store_host( const host& h )
           {
              if( !h.ep.get_address().is_public_address() )
              {
                return false;
              }

              fc::time_point expire_time = fc::time_point::now() - fc::seconds(60*60*3);

              if(  h.last_com < expire_time )
              {
                return false; // too old
              }

              /** remove any expired hosts while we are at it */
              for( auto itr = recent_hosts.begin(); itr != recent_hosts.end();  )
              {
                  if( itr->ep == h.ep )
                  {
                      if( itr->last_com < h.last_com )
                      {
                         itr->last_com = h.last_com;
                      }
                      return false; 
                  }
                  if( itr->last_com < expire_time )
                  {
                    itr = recent_hosts.erase(itr);      
                  }
                  else
                  {
                    ++itr;
                  }
              }
              if( recent_hosts.size() >= 1000 ) return false;
              recent_hosts.push_back(h);
              return true;
           }
           
           virtual void on_connected( const connection_ptr& c )
           {
               c->set_channel_data( channel_id( peer_proto ), std::make_shared<peer_data>() );
               ilog( "on connected..." );
               send_subscription_request( c );
           }
           peer_data& get_channel_data( const connection_ptr& c )
           {
              return c->get_channel_data( channel_id(peer_proto) )->as<peer_data>(); 
           }

           
           virtual void on_disconnected( const connection_ptr& c )
           {
               peer_data& pd = c->get_channel_data( channel_id(peer_proto) )->as<peer_data>(); 
               for( auto itr = pd.subscribed_channels.begin(); itr != pd.subscribed_channels.end(); ++itr )
               {
                  cons_by_channel[*itr].remove_connection(c.get());
               }
           }

           
           virtual void handle_subscribe( const connection_ptr& c )
           {
           }
           virtual void handle_unsubscribe( const connection_ptr& c )
           {
           }

           virtual void handle_message( const connection_ptr& c, const message& m )
           {
               if( m.msg_type == announce_msg::type )
               {
                   handle_announce( c, m.as<announce_msg>() );
               }
               else if( m.msg_type == announce_inv_msg::type )
               {
                   handle_announce_inv( c, m.as<announce_inv_msg>() );
               }
               else if( m.msg_type == get_announce_msg::type )
               {
                   handle_get_announce( c, m.as<get_announce_msg>() );
               }
               else if( m.msg_type == subscribe_msg::type )
               {
                   handle_subscribe( c, m.as<subscribe_msg>() );
               }
               else if( m.msg_type == unsubscribe_msg::type )
               {
                   handle_unsubscribe( c, m.as<unsubscribe_msg>() );
               }
               else if( m.msg_type == known_hosts_msg::type )
               {
                   handle_known_hosts( c, m.as<known_hosts_msg>() );
               }
               else if( m.msg_type == get_known_hosts_msg::type )
               {
                   handle_get_known_hosts( c, m.as<get_known_hosts_msg>() );
               }
               else if( m.msg_type == config_msg::type )
               {
                   handle_config( c, m.as<config_msg>() );
               }
               else if( m.msg_type == error_report_msg::type )
               {
                   handle_error_report( c, m.as<error_report_msg>() );
               }
           }

           void handle_announce( const connection_ptr& c, announce_msg msg  )
           {
              // TODO: validate that we requested this message, otherwise it is an
              // unsolicited message and we must note the misbehavior of the connection

              if( msg.validate_work() )
              {
                  auto itr = known_hosts.find( msg.get_host_id() );
                  if( itr.valid() )
                  {
                     auto old_value = itr.value();
                     if( old_value.timestamp + fc::seconds( 60 * 60 ) > msg.timestamp )
                     {
                       wlog( "connection ${endpoint} sent announcement too soon after last annoucnement", 
                             ("endpoint",c->remote_endpoint()) );
                        // TODO: misbehaving connection ?
                        return; 
                     }
                  }
                  // TODO: check against blacklist... 

                  known_hosts.store( msg.get_host_id(), msg );
                  // add this host id to the current inventory so we can broadcast an INV
                  // for this announcement.
              }
              else
              {
                 wlog( "connection ${endpoint} sent announcement with invalid work", ("endpoint",c->remote_endpoint()) );
                 // TODO: connection is misbehaving!
              }
           }

           void handle_announce_inv( const connection_ptr& c, announce_inv_msg msg )
           {
              peer_data& pd = get_channel_data( c );
              ilog( "inv: ${msg}", ("msg",msg) );
              for( auto itr = msg.announce_msgs.begin(); itr != msg.announce_msgs.end(); ++itr )
              {
                 announce_broadcasts.received_inventory_notice( *itr ); 
              }
              pd.announce_messages.update_known( msg.announce_msgs );
           }

           void handle_get_announce(  const connection_ptr& c, get_announce_msg msg )
           {
               // TODO: how do we prevent the peer from floodign us with get requests?
               // TODO: what do we do if msg.announce_id is invalid?  
               auto reply = announce_broadcasts.get_value( msg.announce_id );
               // TODO: should we verify that the announce message is still valid
               // before sending... it may have expired 
               if( !!reply )
               {
                  c->send( network::message(*reply,_chan_id) );
               }
               else
               {
                  wlog( "unknown peer announcement id ${id}", ("id",msg.announce_id) );
               }
           }

           void handle_config( const connection_ptr& c, config_msg cfg  )
           {
               peer_data& pd = get_channel_data( c );
               pd.peer_config = std::move(cfg);

               if( recent_hosts.size() < PEER_HOST_CACHE_QUERY_LIMIT )
               {
                  pd.requested_hosts = true;
                  c->send( message( get_known_hosts_msg(), channel_id(peer_proto) ) );
               }
           }

           void handle_known_hosts( const connection_ptr& c, const known_hosts_msg& m )
           {
               peer_data& pd = get_channel_data( c );

               std::vector<host> new_hosts;
               for( auto itr = m.hosts.begin(); itr != m.hosts.end(); ++itr )
               {
                  if( store_host( *itr ) && pd.requested_hosts )   
                  {
                    new_hosts.push_back(*itr);
                  }
               }
               pd.requested_hosts = false;

               if( new_hosts.size() )
               {
                  netw->broadcast( message( known_hosts_msg( new_hosts ) , channel_id( peer_proto) ) );
               }
           }

           /**
            *  Sends all channels that this peer is watching to the remote peer. This is typically
            *  only called once on a new connection, after which incremental updates via the
            *  peer_channel::subscribe_to_channel(...) should be used instead.
            */
           void send_subscription_request( const connection_ptr& c )
           {
              std::vector<channel_id> chans;
              chans.reserve( subscribed_channels.size() );
              for( auto itr = subscribed_channels.begin(); itr != subscribed_channels.end(); ++itr )
              {
                chans.push_back( channel_id(*itr) );
              }

              c->send( message( subscribe_msg( std::move(chans) ), channel_id( peer_proto, 0 ) ) ); 
           }
           
           void handle_subscribe( const connection_ptr& c, const subscribe_msg& s )
           {
               ilog( "${ep}: ${msg}", ("ep", c->remote_endpoint()) ("msg",s) );
               peer_data& pd = get_channel_data(c);
               if( s.channels.size() > MAX_CHANNELS_PER_CONNECTION )
               {
                  // TODO... send a error message... disconnect host... require pow?
               }
           
               for( auto itr = s.channels.begin(); itr != s.channels.end(); ++itr )
               {
                   if( pd.subscribed_channels.insert( itr->id() ).second )
                   {
                      // TODO: validate ID is an acceptable / supported channel to prevent
                      // remote hosts from sending us a ton of bogus channels
                      cons_by_channel[itr->id()].add_connection(c.get());

                      auto chan_ptr = netw->get_channel( channel_id(itr->id()) );
                      if( chan_ptr != nullptr )
                      {
                         chan_ptr->handle_subscribe( c );
                      }
                   }
               }
           }
           
           void handle_unsubscribe( const connection_ptr& c, const unsubscribe_msg& s )
           {
               peer_data& pd = c->get_channel_data( channel_id(peer_proto) )->as<peer_data>(); 
               for( auto itr = s.channels.begin(); itr != s.channels.end(); ++itr )
               {
                   if( pd.subscribed_channels.erase( itr->id() ) != 0 )
                   {
                      // TODO: validate ID is an acceptable / supported channel to prevent
                      // remote hosts from sending us a ton of bogus channels
                      cons_by_channel[itr->id()].remove_connection(c.get());

                      auto chan_ptr = netw->get_channel( channel_id(itr->id()) );
                      if( chan_ptr != nullptr )
                      {
                         chan_ptr->handle_unsubscribe( c );
                      }
                   }
               }
           }

           void handle_get_subscribed( const connection_ptr& c, const get_subscribed_msg& m ) 
           {
              // TODO: rate control this query???
              subscribe_msg s;
              for( auto i = subscribed_channels.begin(); i != subscribed_channels.end(); ++i)
              {
                s.channels.push_back(channel_id(*i));
              }
              c->send( message( s, channel_id( peer_proto ) ) );
           }

           void handle_get_known_hosts( const connection_ptr& c, const get_known_hosts_msg& h )
           {
              c->send( message( known_hosts_msg( recent_hosts), channel_id(peer_proto) ) );
           }

           void handle_error_report( const connection_ptr& c, const error_report_msg& m)
           {
              wlog( "${reprot}", ("reprot",m) );
           }
      }; // peer_channel_impl


   } // namespace detail

   peer_channel::peer_channel( const server_ptr& s )
   :my( new detail::peer_channel_impl() )
   {
      my->netw = s;
      my->_chan_id = channel_id( peer_proto, 0 );
      s->set_delegate( my.get() );
      subscribe_to_channel( my->_chan_id, my );
   }

   peer_channel::~peer_channel()
   {
      my->netw->unsubscribe_from_channel( channel_id(peer_proto) );
   }

   void peer_channel::subscribe_to_channel( const channel_id& chan, const channel_ptr& c )
   {
      // let the network know to forward messages to c
      my->netw->subscribe_to_channel( chan, c );
      my->subscribed_channels.insert( chan.id() );

      // let other peers know that we are now subscribed to chan
      subscribe_msg s;
      s.channels.push_back(chan);
      my->netw->broadcast( message( s, channel_id( peer_proto ) ) );
   }

   void peer_channel::unsubscribe_from_channel( const channel_id& chan )
   {
      my->netw->unsubscribe_from_channel(chan);

      // let other peers know that we are now unsubscribed from chan
      unsubscribe_msg u;
      u.channels.push_back(chan);
      my->netw->broadcast( message( u, channel_id( peer_proto ) ) );
   }


   std::vector<network::connection_ptr> peer_channel::get_connections( const network::channel_id& chan )
   {
      std::vector<network::connection_ptr> cons;

      auto itr = my->cons_by_channel.find( chan.id() );
      if( itr != my->cons_by_channel.end() )
      {
         return itr->second.get_connections(); 
      }
      wlog( "no connections for channel ${chan}", ("chan",chan) );
      return cons;
   }


} } // bts::network
