#include <bts/bitname/bitname_db.hpp>
#include <bts/difficulty.hpp>
#include <bts/blockchain/blockchain_time_keeper.hpp>
#include <bts/config.hpp>
#include <bts/db/level_map.hpp>
#include <bts/db/level_pod_map.hpp>
#include <fc/io/raw.hpp>
#include <fc/io/fstream.hpp>
#include <fc/reflect/variant.hpp>
#include <unordered_map>

#include <iostream> // TODO: remove dep
#include <iomanip> // TODO: remove dep
#include <fc/io/json.hpp>

static const uint16_t max_trx_num = uint16_t(-1);
struct name_location 
{
    name_location( uint32_t block_num= 0, uint16_t tx_num = 0)
    :block_num(block_num),trx_num(tx_num){}

    uint32_t block_num;
    uint16_t trx_num; /// trx_num max_trx_num == header name
};
FC_REFLECT( name_location, (block_num)(trx_num) )

namespace bts { namespace bitname {

    namespace ldb = leveldb;
   
    namespace detail 
    {
       class name_db_impl 
       {
          public:
             name_db_impl()
             :_chain_difficulty(0)
             {
             }

             /** map block number to header */
             db::level_pod_map<uint32_t, name_header>                 _block_num_to_header;

             /** map block number to the trxs used in that block */
             db::level_pod_map<uint32_t, std::vector<name_trx> >      _block_num_to_name_trxs;

             /** tracks this history of every name and where it can be found in the chain 
              *  TODO: verify that name_location are sorted by depth... 
              **/
             db::level_pod_map<uint64_t, std::vector<name_location> > _name_hash_to_locs;

             blockchain::time_keeper   _timekeeper;

             /** the id of every header back to the genesis, index is block_num 
              *
              * This can be reconstructed from _block_num_to_block if there is
              * any corruption on load.
              **/
             std::vector<fc::sha224>                    _header_ids;
             uint64_t                                   _chain_difficulty;

             /** rapid lookup of block_num from block_id, this can be built
              * from _header_ids
              **/
             std::unordered_map<fc::sha224,uint32_t>   _id_to_block_num;


             name_location find_name( uint64_t name )
             {
               auto name_locs = _name_hash_to_locs.fetch( name );
               FC_ASSERT( name_locs.size() != 0 );
               return name_locs.back();
             }

             void index_trx( const name_location& loc, uint64_t name_hash )
             {
                auto name_locs_itr = _name_hash_to_locs.find( name_hash );
                if( name_locs_itr.valid() )
                {
                    auto name_locs = name_locs_itr.value();
                    name_locs.push_back( loc );
                    _name_hash_to_locs.store( name_hash, name_locs );
                }
                else
                {
                    _name_hash_to_locs.store( name_hash, std::vector<name_location>(1,loc) );
                }
             }

             void load_indexes( const fc::path& db_dir )
             {
                ilog( "load indexes" );
                 bool rebuild_header_ids = true;
                 if( fc::exists( db_dir / "header_ids" ) )
                 {
                   //  fc::ifstream in( db_dir / "header_ids", fc::ifstream::binary );
                    // fc::raw::unpack( in, _header_ids );
                 
                     // TODO: validate integrety
                 
                     // rebuild_header_ids = false;
                     // TODO: load chain difficulty here as well.
                 }
                 
                 if( rebuild_header_ids )
                 {
                    ilog( "load indexes" );
                    auto itr = _block_num_to_header.begin();
                    while( itr.valid() )
                    {
                      push_header_id( itr.value().id() );
                      ++itr;
                    }
                    // TODO: save to disk
                 }
             }

             void load_genesis()
             { try {
                 // TODO: verify that all databases are NULL/empty 
                
                 auto genesis = create_genesis_block(); 
                 _timekeeper.configure( genesis.utc_sec, 
                                        fc::seconds( BITNAME_BLOCK_INTERVAL_SEC ),
                                        BITNAME_TIMEKEEPER_WINDOW );
                 
                 if( _header_ids.size() == 0 )
                 {
                    _block_num_to_header.store( 0, genesis );
                    _block_num_to_name_trxs.store( 0, std::vector<name_trx>() );
                    index_trx( name_location( 0, max_trx_num ), genesis.name_hash );
                    push_header_id( genesis.id() );
                 }
             } FC_RETHROW_EXCEPTIONS( warn, "" ) }

             void push_header_id( const fc::sha224& id )
             {
                // TODO: consider using boost::multiindex 
                _header_ids.push_back(id);
                _chain_difficulty += bts::difficulty(id);
                _id_to_block_num[id] = _header_ids.size()-1;
             }

             void init_timekeeper()
             {
                uint32_t window_start = 0;
                /*
                if( _header_ids.size() > BITNAME_TIMEKEEPER_WINDOW )
                {
                    window_start = _header_ids.size() - BITNAME_TIMEKEEPER_WINDOW;
                }
                */

                for( uint32_t window_pos = window_start; 
                     window_pos < _header_ids.size(); ++window_pos )
                {
                   auto head = fetch_block_header( window_pos );
                   FC_ASSERT( head.id() == _header_ids[window_pos] ); // sanity check
                   auto dif           = head.difficulty();
                   _timekeeper.push_init( window_pos, head.utc_sec, dif );
                }
               ilog( "...init stats..." );
                _timekeeper.init_stats();
               ilog( "...done init timekeeper..." );
             }

             name_header fetch_block_header( uint32_t block_num )
             { try {
                 return _block_num_to_header.fetch( block_num );
             } FC_RETHROW_EXCEPTIONS( warn, "unable to fetch block header for block num ${block_num}", ("block_num",block_num) ) }

             name_block   fetch_block( uint32_t block_num )
             { try {
                 auto header = fetch_block_header( block_num );
                 name_block block(header);
                 block.name_trxs = _block_num_to_name_trxs.fetch( block_num );
                 FC_ASSERT( block.trxs_hash == block.calc_trxs_hash() );
                 return block;
             } FC_RETHROW_EXCEPTIONS( warn, "unable to fetch block num ${block_num}", ("block_num",block_num) ) }
       };
    } // namespace detail

    name_db::name_db()
    :my( new detail::name_db_impl() ){}
    name_db::~name_db()
    {
      try {
       close(); 
      } 
      catch( const fc::exception& e )
      {
        wlog( "exception ${e}", ("e",e.to_detail_string() ) );
      }
    }

    void name_db::open( const fc::path& db_dir, bool create )
    { try {
       if( !fc::exists( db_dir ) )
       {
         if( !create )
         {
            FC_THROW_EXCEPTION( file_not_found_exception, "Unable to open name database ${dir}", ("dir",db_dir) );
         }
         fc::create_directories( db_dir );
       }

       my->_block_num_to_header.open( db_dir / "block_num_to_header" );
       my->_block_num_to_name_trxs.open( db_dir / "block_num_to_name_trxs" );
       my->_name_hash_to_locs.open( db_dir / "name_hash_to_locs" );

       my->load_indexes(db_dir);
       my->load_genesis();
       my->init_timekeeper();
       ilog( "open name db" );
       dump(); // DEBUG
    } FC_RETHROW_EXCEPTIONS( warn, "unable to open name db at path ${path}", ("path", db_dir)("create",create) ) }

    void name_db::close()
    { try {
       // TODO: save _header_ids to disk
       my->_block_num_to_header.close();
       my->_block_num_to_name_trxs.close();
       my->_name_hash_to_locs.close();
    } FC_RETHROW_EXCEPTIONS( warn, "" ) }

    uint64_t name_db::target_name_difficulty()const
    { try {
      uint64_t next_dif =  my->_timekeeper.next_difficulty();
      next_dif /= 10000; // TODO: document this magic number, move to config.hpp

      if( next_dif < min_name_difficulty() )
      {
        next_dif = min_name_difficulty();
      }
      return next_dif;
    } FC_RETHROW_EXCEPTIONS( warn, "" ) }

    uint64_t name_db::target_difficulty()const
    { try {
      uint64_t next_dif =  my->_timekeeper.next_difficulty();
      if( next_dif < min_name_difficulty() )
      {
        next_dif = min_name_difficulty();
      }
      return next_dif;
    } FC_RETHROW_EXCEPTIONS( warn, "" ) }


    void name_db::push_block( const name_block& next_block )
    { try {
       FC_ASSERT( next_block.calc_trxs_hash() == next_block.trxs_hash );
       fc::sha224 next_id = next_block.id();
       uint64_t   block_diff = next_block.block_difficulty();

       /**
        *   The difficulty of the block header must be more than the sum of the difficulties of
        *   the contained transactions.
        */
       FC_ASSERT( 2*next_block.difficulty() >= block_diff );

       if( next_block.prev != my->_header_ids.back() )
       {
          FC_ASSERT( next_id < my->_header_ids.back() );
          if( my->_header_ids.size() > 1 )
          {
            FC_ASSERT( next_block.prev == my->_header_ids[ my->_header_ids.size() - 2 ] );
            // we have a potential canidate for replacing the head block.
            wlog( "potential canidate for replacing head block found" );
            wlog( "save and then pop the current head" );

            auto head_num = head_block_num();
            auto old_head = fetch_block( head_num );

            FC_ASSERT( next_block.id() < old_head.id() );

            try {
                pop_block();
                push_block( next_block );
                wlog( "replaced head block" );
                return;
            } 
            catch ( fc::exception& e )
            {
                push_block( old_head );
                FC_RETHROW_EXCEPTION( e, warn, "unable to replace head block" );
            }
          }
       }

       FC_ASSERT( next_block.prev               == my->_header_ids.back(), "", ("head_block_id",my->_header_ids.back()) );
       FC_ASSERT( next_block.trxs_hash          == next_block.calc_trxs_hash() );
       FC_ASSERT( next_block.difficulty() >= target_difficulty() );
       /*
       FC_ASSERT( next_block.difficulty()   >= next_block.block_difficulty()/2,
                 "",("next_id",next_id)("difficulty(next_id)",bts::difficulty(next_id))("next_block_dif", next_block.block_difficulty()));
          */
       uint64_t trx_target = target_name_difficulty();
       // check work first, this doesn't involve db queries
       for( uint32_t trx = 0; trx < next_block.name_trxs.size(); ++trx )
       {
          FC_ASSERT( next_block.name_trxs[trx].difficulty(next_block.prev) >= trx_target );
       }

       // the header has some special rules that don't apply to normal trx, like
       // being able to change a public key
       validate_trx( next_block, true /*is_header*/ );

       // now validate trxs against db state
       size_t num_trx =  next_block.name_trxs.size();
       for( uint32_t trx_idx = 0; trx_idx < num_trx; ++trx_idx )
       {
          validate_trx( next_block.name_trxs[trx_idx] );
       }

       // TODO: If something fails during this operation, we need to make sure
       // that the name_db is left in the prior state.
       
       uint32_t next_num = my->_header_ids.size();
       my->push_header_id( next_id );
       my->_block_num_to_header.store( next_num, next_block );
       my->_block_num_to_name_trxs.store( next_num, next_block.name_trxs );
       
       for( uint16_t trx_idx = 0; trx_idx < num_trx; ++trx_idx )
       {
          my->index_trx( name_location( next_num, trx_idx ), next_block.name_trxs[trx_idx].name_hash );
       }
       my->index_trx( name_location( next_num, max_trx_num ), next_block.name_hash );
       my->_timekeeper.push( next_num, next_block.utc_sec, next_block.difficulty() );
    } FC_RETHROW_EXCEPTIONS( warn, "unable to push block ${next_block}", ("next_block", next_block) ) } 


    /**
     *  checks to make sure the transaction is valid and could be applied to the
     *  current database.
     */
    void name_db::validate_trx( const name_trx& trx, bool is_header )const
    { try {
       fc::sha224 chain_head_id = my->_header_ids.back();
       FC_ASSERT( trx.utc_sec > (fc::time_point(chain_time()) - fc::seconds( BITNAME_TIME_TOLLERANCE_SEC ) ),
                  "trx.utc_sec: ${trx_time}   chain_time: ${chain_time}", 
                  ("trx_time",trx.utc_sec)
                  ("expected",my->_timekeeper.expected_time(head_block_num()+1))
                  ("chain_time",chain_time()));
       FC_ASSERT( trx.difficulty( chain_head_id ) >= target_name_difficulty(), "perhaps wrong previous node?", ("chain_head_id",chain_head_id)("trx_id",trx.id(chain_head_id)) );

       auto prev_reg_itr = my->_name_hash_to_locs.find( trx.name_hash );

       if( prev_reg_itr.valid() ) // renewal... 
       {
          std::vector<name_location> name_locs = prev_reg_itr.value();
          FC_ASSERT( name_locs.size() > 0 );
          name_location prev_loc = name_locs.back();

//          ilog( "prev_loc.block_num ${block_num}", ("block_num",prev_loc.block_num) );
          std::vector<name_trx>  prev_block_trxs = my->_block_num_to_name_trxs.fetch( prev_loc.block_num );
 //         ilog( "prev block trxs: ${trx}", ("trx",prev_block_trxs) );
          name_trx  prev_trx;
          
          uint32_t repute = 1;
          if( prev_loc.trx_num == max_trx_num ) // then the last block earned me points!
          {
             repute += prev_block_trxs.size();
  //           ilog( "prev_loc.block_num ${block_num}", ("block_num",prev_loc.block_num) );
             prev_trx = my->_block_num_to_header.fetch( prev_loc.block_num );
          }
          else
          {
             prev_trx  = prev_block_trxs[prev_loc.trx_num];
          }

          if( trx.repute_points != fc::unsigned_int(0) ) // this is an update
          {
             FC_ASSERT( trx.repute_points.value == prev_trx.repute_points.value + repute, "", ("prev_trx", prev_trx)("repute",repute) );
             FC_ASSERT( trx.master_key  == prev_trx.master_key );
             FC_ASSERT( trx.age  == prev_trx.age );
             if( trx.active_key != prev_trx.active_key )
             {
                 auto digest = fc::sha256::hash( trx.active_key.data, trx.active_key.size() );
                 FC_ASSERT( trx.master_key == fc::ecc::public_key(*trx.change_sig,digest) );
             }
          }
          else // this is a transfer or cancel
          {
             FC_ASSERT( trx.age == my->_header_ids.size() );
             FC_ASSERT( !!trx.change_sig );

             auto last_update = (my->_header_ids.size() - prev_loc.block_num);
             
             auto trx_id = trx.id(chain_head_id);
             auto digest = fc::sha256::hash( (char*)&trx_id, sizeof(trx_id) );
             fc::ecc::public_key signed_key( *trx.change_sig, digest );
             if( trx.master_key == fc::ecc::public_key_data() )  // this is a cancel attempt
             {
                 // cancel attempt within the transfer window, must be signed
                 // by the prior public key rather than the new public key.
                 if( last_update < BITNAME_BLOCKS_BEFORE_TRANSFER )
                 {
                     FC_ASSERT( name_locs.size() > 2 );
                     auto prev_prev_update_loc = name_locs[name_locs.size()-2];
                     if( prev_prev_update_loc.trx_num == max_trx_num )
                     {
   //                       ilog( "prev_prev_update_loc.block_num ${block_num}", ("block_num",prev_prev_update_loc.block_num) );
                        auto prev_prev_header =  my->_block_num_to_header.fetch( prev_prev_update_loc.block_num );
                        FC_ASSERT( prev_prev_header.master_key == signed_key );
                     }
                     else
                     {
    //                      ilog( "prev_prev_update_loc.block_num ${block_num}", ("block_num",prev_prev_update_loc.block_num) );
                        std::vector<name_trx>  prev_prev_block_trxs = my->_block_num_to_name_trxs.fetch( prev_prev_update_loc.block_num );
                        FC_ASSERT( prev_prev_block_trxs.size() > prev_prev_update_loc.trx_num );
                        FC_ASSERT( prev_prev_block_trxs[prev_prev_update_loc.trx_num].master_key == signed_key );
                     }
                 }
                 else // we are outside the transfer window, we can cancel with current key
                 {
                     FC_ASSERT( signed_key == prev_trx.master_key );
                 }
             }
             else // this is a transfer attempt
             {
                 FC_ASSERT( is_header );
                 FC_ASSERT( signed_key == prev_trx.master_key );
                 FC_ASSERT( last_update > BITNAME_BLOCKS_BEFORE_TRANSFER );
             }
          }
       }
       else // new registration...
       {
          FC_ASSERT( trx.age == my->_header_ids.size(), "", ("header_ids.size()", my->_header_ids.size()) );
          FC_ASSERT( trx.repute_points == fc::unsigned_int(1) );
          FC_ASSERT( trx.master_key != fc::ecc::public_key_data() );
          FC_ASSERT( trx.name_hash > 1000 ); // first 1000 hash slots are reserved for future use
       }
    } FC_RETHROW_EXCEPTIONS( warn, "error validating ${trx} header: ${is_header}", 
                                      ("trx", trx)("is_header",is_header) ) }
   
    void name_db::pop_block()
    { try {
        auto head_num = head_block_num();
        auto old_head = fetch_block( head_num );
        for( uint32_t i = 0; i < old_head.name_trxs.size(); ++i )
        {
           std::vector<name_location> locs = my->_name_hash_to_locs.fetch( old_head.name_trxs[i].name_hash ); 
           if( locs.back().block_num == head_num )
           {
             locs.pop_back();
           }
           else
           {
             FC_ASSERT( !"index appears to be corrupt, you might want to fix that." );
           }
           if( locs.size() )
           {
               my->_name_hash_to_locs.store( old_head.name_trxs[i].name_hash, locs ); 
           }
           else
           {
               my->_name_hash_to_locs.remove( old_head.name_trxs[i].name_hash );
           }
        }
        std::vector<name_location> locs = my->_name_hash_to_locs.fetch( old_head.name_hash ); 
        if( locs.back().block_num == head_num )
        {
          locs.pop_back();
        }
        else
        {
          FC_ASSERT( !"index appears to be corrupt, you might want to fix that." );
        }
        if( locs.size() == 0 )
        {
            my->_name_hash_to_locs.remove( old_head.name_hash );
        }
        else
        {
            my->_name_hash_to_locs.store( old_head.name_hash, locs ); 
        }


        my->_block_num_to_header.remove( head_num );
        my->_block_num_to_name_trxs.remove( head_num );
        my->_timekeeper.pop( head_num );
        my->_id_to_block_num.erase( old_head.id() );
        my->_chain_difficulty -= old_head.difficulty();
        my->_header_ids.pop_back();
    } FC_RETHROW_EXCEPTIONS( warn, "" ) }
    

    uint32_t   name_db::head_block_num()const
    {
      FC_ASSERT( my->_header_ids.size() != 0 );
      return my->_header_ids.size() - 1;
    }
    fc::sha224 name_db::head_block_id()const 
    { try { 
      FC_ASSERT( my->_header_ids.size() != 0 ); 
      return my->_header_ids.back();
    } FC_RETHROW_EXCEPTIONS( warn, "" ) }

    uint32_t   name_db::get_expiration( uint64_t name_hash ) const
    { try {
      auto locs = my->_name_hash_to_locs.fetch(name_hash);
      return locs.back().block_num + BITNAME_BLOCKS_PER_YEAR;
    } FC_RETHROW_EXCEPTIONS( warn, "" ) }

    name_trx   name_db::fetch_trx( uint64_t name_hash )const
    { try {
        auto name_loc  = my->find_name( name_hash );
        if( name_loc.trx_num != max_trx_num )
        {
          auto name_trxs = my->_block_num_to_name_trxs.fetch( name_loc.block_num );
          FC_ASSERT( name_trxs.size() > name_loc.trx_num, "trx_num: ${num}", ("num",name_loc.trx_num) );
          FC_ASSERT( name_trxs[name_loc.trx_num].name_hash == name_hash );
          return name_trxs[name_loc.trx_num];
        }
        else
        {
          auto name_head = my->_block_num_to_header.fetch( name_loc.block_num );
          FC_ASSERT( name_head.name_hash == name_hash );
          return name_head;
        }
    } FC_RETHROW_EXCEPTIONS( warn, "unable to fetch trx for name hash ${name_hash}", ("name_hash", name_hash ) ) }

    uint32_t name_db::fetch_repute( uint64_t name_hash )const
    {
        auto name_loc  = my->find_name( name_hash );
        if( name_loc.trx_num != max_trx_num )
        {
          auto name_trxs = my->_block_num_to_name_trxs.fetch( name_loc.block_num );
          FC_ASSERT( name_trxs.size() > name_loc.trx_num, "trx_num: ${num}", ("num",name_loc.trx_num) );
          FC_ASSERT( name_trxs[name_loc.trx_num].name_hash == name_hash );
          return name_trxs[name_loc.trx_num].repute_points.value;
        }
        else
        {
          auto name_head = my->_block_num_to_header.fetch( name_loc.block_num );
          FC_ASSERT( name_head.name_hash == name_hash );
          return name_head.repute_points.value + my->_block_num_to_name_trxs.fetch( name_loc.block_num ).size();
        }
    }


    name_header name_db::fetch_block_header( const fc::sha224& block_id )const
    { try {
        return fetch_block_header( get_block_num( block_id ) );
    } FC_RETHROW_EXCEPTIONS( warn, "unable to fetch block header forid ${block_id}", ("block_id",block_id) ) }


    name_header name_db::fetch_block_header( uint32_t block_num )const
    { 
        return my->fetch_block_header(block_num);
    } 


    name_block   name_db::fetch_block( uint32_t block_num )const
    { 
        return my->fetch_block(block_num);
    }


    name_block   name_db::fetch_block( const fc::sha224& block_id )const
    { try {
        return fetch_block( get_block_num( block_id ) );
    } FC_RETHROW_EXCEPTIONS( warn, "unable to fetch block id ${block_id}", ("block_id",block_id) ) }


    fc::time_point_sec name_db::chain_time()const
    { try {
      return my->_timekeeper.current_time();
    } FC_RETHROW_EXCEPTIONS( warn, "" ) }

    /**
     *
     */
    fc::time_point_sec name_db::expected_time( uint32_t block_num )const
    { try {
      return my->_timekeeper.expected_time(block_num);
    } FC_RETHROW_EXCEPTIONS( warn, "" ) }

    /**
     */
    uint32_t name_db::get_block_num( const fc::sha224& block_id )const
    { try {
        auto block_num_itr = my->_id_to_block_num.find(block_id);
        FC_ASSERT( block_num_itr != my->_id_to_block_num.end() );
        return block_num_itr->second;
    } FC_RETHROW_EXCEPTIONS( warn, "unable to fetch block num for id ${block_id}", ("block_id",block_id) ) }

    void name_db::dump()
    {
       /*{
          auto itr = my->_name_hash_to_locs.begin();
          ilog( "name to locs\n--------------------------------------" );
          while( itr.valid() )
          {
             ilog( "${key} => ${val}", ("key",itr.key())("val",itr.value()) );
             ++itr;
          }
          wlog( "header_ids ${ids}", ("ids",my->_header_ids) );

       } // END DEBUG
       */
       auto genesis = create_genesis_block(); 
       blockchain::time_keeper timekeep;
       timekeep.configure( genesis.utc_sec, 
                              fc::seconds( BITNAME_BLOCK_INTERVAL_SEC ),
                              BITNAME_TIMEKEEPER_WINDOW );
       timekeep.push_init( 0, genesis.utc_sec, genesis.block_difficulty() );
       timekeep.init_stats();

       uint32_t start = 0;
       if( my->_header_ids.size() > BITNAME_TIMEKEEPER_WINDOW )
           start = my->_header_ids.size() - BITNAME_TIMEKEEPER_WINDOW;
       for( uint32_t i = 0; i < my->_header_ids.size(); ++i )
       {
          auto blkhead = fetch_block_header(i); //my->_block_num_to_header.fetch(i); 
          if( i > start )
          {
             std::cerr<<std::setw(3)  << i                                           <<"] "
                      <<std::setw(20) << fc::variant(my->_header_ids[i]).as_string() <<" ";
             
             std::cerr<<"name: "<<std::setw(24) << blkhead.name_hash << " ";
             std::cerr<<"time: "<<std::setw(16) << std::string(fc::time_point(blkhead.utc_sec)) <<" ";
             std::cerr<<"age: "<<std::setw(5) << blkhead.age <<" ";
             std::cerr<<"repute: "<<std::setw(5) << blkhead.repute_points.value <<" ";
             std::cerr<<"prev: "<<std::setw(10) << fc::variant(blkhead.prev).as_string().substr(0,10) <<" ";
             std::cerr<<"master key: "<<std::setw(12)<<fc::variant(blkhead.master_key).as_string().substr(0,10)<<" ";
             std::cerr<<"active key: "<<std::setw(12)<<fc::variant(blkhead.active_key).as_string().substr(0,10)<<" ";
             std::cerr<<"med interval: "<<std::setw(6)<<timekeep.median_interval()<<" sec ";
             std::cerr<<"target interval: "<<std::setw(6)<<timekeep.target_interval()<<" sec ";
             std::cerr<<"curr_diff: "<<std::setw(14)<<timekeep.current_difficulty()<<" ";
             std::cerr<<"next_diff: "<<std::setw(14)<<timekeep.next_difficulty()<<" ";
             std::cerr<<"block_diff: "<<std::setw(14)<<blkhead.difficulty()<<" ";
             std::cerr<<"trx_ diff: "<<std::setw(14)<<blkhead.difficulty()<<" ";
             std::cerr<<"chain drift: "<<std::setw(8)<<timekeep.current_time_error() << " sec ";
             auto delta = (timekeep.expected_time(i)-blkhead.utc_sec);
             std::cerr<<"real drift: "<<std::setw(8)<<delta.count()/1000000 << " sec ";
             std::cerr<<"trxs: "<<fc::variant(blkhead.trxs_hash).as_string().substr(0,8)<<"";

             std::cerr<<"\n";
             auto blk = fetch_block(i); //my->_block_num_to_header.fetch(i); 
             std::vector<name_trx> blktrxs = my->_block_num_to_name_trxs.fetch(i);
             for( uint32_t t = 0; t < blk.name_trxs.size(); ++t )
             {
                std::cerr<<"\t\t"<<std::setw(3)<<t<<") ";
                std::cerr<<std::setw(24)<<blktrxs[t].name_hash<<" ";
                std::cerr<<"time: "<<std::setw(16)<<std::string( fc::time_point(blktrxs[t].utc_sec))<<" ";
                std::cerr<<"age: "<<std::setw(3)<<blktrxs[t].age<<" ";
                std::cerr<<"repute: "<<std::setw(3)<<blktrxs[t].repute_points.value<<" ";
                std::cerr<<"difficulty: "<<std::setw(3)<<blktrxs[t].difficulty(blkhead.prev)<<" ";
                std::cerr<<"key: "<<std::setw(66)<<fc::json::to_string(blktrxs[t].master_key)<<" ";
                std::cerr<<"\n";
             }
          }
          if( i > 0 )
          {
            timekeep.push( i, blkhead.utc_sec, blkhead.difficulty() );
          }
       }
       std::cerr<<"chain time: "<<std::string( fc::time_point(chain_time()) )<<"\n";
       std::cerr<<"expected time: "<<std::string( fc::time_point(expected_time( my->_header_ids.size()-1)) )<<"\n";
       std::cerr<<"target difficulty: "<<target_difficulty()<<"\n";
       std::cerr<<"target name difficulty: "<<target_name_difficulty()<<"\n";
    }
    const std::vector<name_id_type>&  name_db::get_header_ids()const
    {
      return my->_header_ids;
    }
    uint64_t name_db::chain_difficulty()const
    {
      return my->_chain_difficulty;
    }

} } // bts::bitname
