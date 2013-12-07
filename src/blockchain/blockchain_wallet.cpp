#include <bts/blockchain/blockchain_wallet.hpp>
#include <bts/blockchain/asset.hpp>
#include <bts/blockchain/block.hpp>
#include <bts/extended_address.hpp>
#include <unordered_map>
#include <fc/reflect/variant.hpp>
#include <fc/filesystem.hpp>
#include <fc/io/raw.hpp>
#include <fc/log/logger.hpp>

#include <iostream>

namespace bts { namespace blockchain {
   struct wallet_data 
   {
       extended_private_key                                 base_key; 
       uint32_t                                             last_used_key;
       uint32_t                                             last_scanned_block_num;
       std::unordered_map<bts::address,std::string>         recv_addresses;
       std::unordered_map<bts::address,std::string>         send_addresses;
       std::vector<fc::ecc::private_key>                    extra_keys;
       std::vector<bts::blockchain::signed_transaction>     out_trx;
       std::vector<bts::blockchain::signed_transaction>     in_trx;
   };
} } // bts::blockchain

FC_REFLECT( bts::blockchain::wallet_data, 
            (base_key)
            (last_used_key)
            (last_scanned_block_num)
            (recv_addresses)
            (send_addresses)
            (extra_keys)
            (out_trx)
            (in_trx) )

namespace bts { namespace blockchain {
  
   namespace detail 
   {
      class wallet_impl
      {
          public:
              wallet_impl():_stake(0){}

              fc::path                                                   _wallet_dat;
              wallet_data                                                _data;
              asset                                                      _current_fee_rate;
              uint64_t                                                   _stake;

              std::unordered_map<output_reference,trx_output>            _unspent_outputs;
              std::unordered_map<output_reference,trx_output>            _spent_outputs;

              // maps address to private key index
              std::unordered_map<bts::address,uint32_t>                  _my_addresses;
              std::unordered_map<transaction_id_type,signed_transaction> _id_to_signed_transaction;


              /**
               *  Collect inputs that total to at least min_amnt.
               */
              std::vector<trx_input> collect_inputs( const asset& min_amnt, asset& total_in, std::unordered_set<bts::address>& req_sigs )
              {
                   std::vector<trx_input> inputs;
                   for( auto itr = _unspent_outputs.begin(); itr != _unspent_outputs.end(); ++itr )
                   {
                       if( itr->second.claim_func == claim_by_signature && itr->second.unit == min_amnt.unit )
                       {
                           inputs.push_back( trx_input( itr->first ) );
                           total_in += itr->second.get_amount();
                           req_sigs.insert( itr->second.as<claim_by_signature_output>().owner );

                           if( total_in >= min_amnt )
                           {
                              return inputs;
                           }
                       }
                   }
                   FC_ASSERT( !"Unable to collect sufficient unspent inputs", "", ("min_amnt",min_amnt) );
              }
              void sign_transaction( signed_transaction& trx, const std::unordered_set<address>& addresses )
              {
                   for( auto itr = addresses.begin(); itr != addresses.end(); ++itr )
                   {
                      self->sign_transaction( trx, *itr );
                   }
              }
              wallet* self;
      };
   } // namespace detail

   wallet::wallet()
   :my( new detail::wallet_impl() )
   {
      my->self = this;
   }

   wallet::~wallet(){}

   void wallet::open( const fc::path& wallet_dat )
   {
      my->_wallet_dat = wallet_dat;
   }

   void wallet::save()
   {

   }

   asset wallet::get_balance( asset::type t )
   {
      return asset();
   }

   void           wallet::set_stake( uint64_t stake )
   {
      my->_stake = stake;
   }

   void           wallet::import_key( const fc::ecc::private_key& key )
   {
      my->_data.extra_keys.push_back(key);
      my->_my_addresses[ key.get_public_key() ] = my->_data.extra_keys.size() -1;
   }

   bts::address   wallet::get_new_address()
   {
      my->_data.last_used_key++;
      auto new_key = my->_data.base_key.child( my->_data.last_used_key );
      import_key(new_key);
      bts::address addr = new_key.get_public_key();
      return  new_key.get_public_key();
   }

   void                  wallet::set_fee_rate( const asset& pts_per_byte )
   {
      my->_current_fee_rate = pts_per_byte;
   }

   signed_transaction    wallet::transfer( const asset& amnt, const bts::address& to )
   {
       auto   change_address = get_new_address();

       std::unordered_set<bts::address> req_sigs; 
       asset  total_in;

       signed_transaction trx; 
       trx.inputs = my->collect_inputs( amnt, total_in, req_sigs );

       asset change = total_in - amnt;

       trx.outputs.push_back( trx_output( claim_by_signature_output( to ), amnt) );
       trx.outputs.push_back( trx_output( claim_by_signature_output( change_address ), change) );

       trx.sigs.clear();
       my->sign_transaction( trx, req_sigs );

       uint32_t trx_bytes = fc::raw::pack( trx ).size();
       asset    fee( my->_current_fee_rate * trx_bytes );

       if( amnt.unit == asset::bts )
       {
          if( total_in >= amnt + fee )
          {
              change = change - fee;
              trx.outputs.back() = trx_output( claim_by_signature_output( change_address ), change );
              if( change == asset() ) trx.outputs.pop_back(); // no change required
          }
          else
          {
             elog( "NOT ENOUGH TO COVER AMOUNT + FEE... GRAB MORE.." );
              // TODO: this function should be recursive here, but having 2x the fee should be good enough
              fee = fee + fee; // double the fee in this case to cover the growth
              req_sigs.clear();
              total_in = asset();
              trx.inputs = my->collect_inputs( amnt+fee, total_in, req_sigs );
              change =  total_in - amnt - fee;
              trx.outputs.back() = trx_output( claim_by_signature_output( change_address ), change );
              if( change == asset() ) trx.outputs.pop_back(); // no change required
          }
       }
       else /// fee is in bts, but we are transferring something else
       {
           if( change.amount == 0 ) trx.outputs.pop_back(); // no change required

           // TODO: this function should be recursive here, but having 2x the fee should be good enough, some
           // transactions may overpay in this case, but this can be optimized later to reduce fees.. for now
           fee = fee + fee; // double the fee in this case to cover the growth
           asset total_fee_in;
           auto extra_in = my->collect_inputs( fee, total_fee_in, req_sigs );
     //      trx.inputs.insert( trx.inputs.end(), extra_in.begin(), extra_in.end() );
     //      trx.outputs.push_back( trx_output( claim_by_signature_output( change_address ), total_fee_in - fee ) );
       }

       trx.sigs.clear();
       my->sign_transaction(trx, req_sigs);


       for( auto itr = trx.inputs.begin(); itr != trx.inputs.end(); ++itr )
       {
           mark_as_spent( itr->output_ref );
       }
       
       return trx;
   }
   void wallet::mark_as_spent( const output_reference& r )
   {
      wlog( "MARK SPENT ${s}", ("s",r) );
      auto itr = my->_unspent_outputs.find(r);
      if( itr == my->_unspent_outputs.end() )
      {
          wlog( "... unknown output.." );
          return;
      }
      my->_spent_outputs[r] = itr->second;
      my->_unspent_outputs.erase(r);
   }

   void wallet::sign_transaction( signed_transaction& trx, const bts::address& addr )
   {
      ilog( "Sign ${trx}  ${addr}", ("trx",trx.id())("addr",addr));
      auto priv_key_idx = my->_my_addresses.find(addr);
      FC_ASSERT( priv_key_idx != my->_my_addresses.end() );
      trx.sign( my->_data.extra_keys[priv_key_idx->second] );
   }

   signed_transaction    wallet::bid( const asset& amnt, const price& ratio )
   {
       signed_transaction trx; 
       return trx;
   }

   signed_transaction    wallet::cancel_bid( const transaction_id_type& bid )
   {
       signed_transaction trx; 
       return trx;
   }
  
   /**
    *  Creates a transaction with two outputs:
    *      1) claim_by_signature of amnt spendable by this wallet
    *      2) claim_by_cover of collateral coverable by this wallet 
    *
    *  Whether or not this transaction is valid depends upon the current market price
    *  of amnt.unit relative to PTS.  This method does not concern itself with the details
    *  of whether or not the resulting transaction is valid, because that depends upon 
    *  changing context.  
    *
    *  To be valid at the time it is included in a block, collateral must be worth 2x the
    *  value of amnt.
    *
    *  @param collateral.unit must be PTS
    */
   signed_transaction    wallet::borrow( const asset& amnt, const asset& collateral )
   {
       signed_transaction trx; 

       return trx;
   }

   signed_transaction    wallet::cover( const asset& amnt )
   {
       signed_transaction trx; 
       return trx;
   }

   /**
    *  Scan the blockchain starting from_block_num until the head block, check every
    *  transaction for inputs or outputs accessable by this wallet.
    */
   void wallet::scan_chain( blockchain_db& chain, uint32_t from_block_num )
   { try {
       auto head_block_num = chain.head_block_num();
       // for each block
       for( uint32_t i = from_block_num; i <= head_block_num; ++i )
       {
          ilog( "block: ${i}", ("i",i ) );
          auto blk = chain.fetch_full_block( i );
          // for each transaction
          for( uint32_t trx_idx = 0; trx_idx < blk.trx_ids.size(); ++trx_idx )
          {
              ilog( "trx: ${trx_idx}", ("trx_idx",trx_idx ) );
              auto trx = chain.fetch_trx( trx_num( i, trx_idx ) ); //blk.trx_ids[trx_idx] );
              ilog( "${id} \n\n  ${trx}\n\n", ("id",trx.id())("trx",trx) );

              // for each output
              for( uint32_t out_idx = 0; out_idx < trx.outputs.size(); ++out_idx )
              {
                  const trx_output& out = trx.outputs[out_idx];
                  switch( out.claim_func )
                  {
                     case claim_by_signature:
                     {
                        auto owner = out.as<claim_by_signature_output>().owner;
                        auto aitr  = my->_my_addresses.find(owner);
                        if( aitr != my->_my_addresses.end() )
                        {
                            if( !trx.meta_outputs[out_idx].is_spent() )
                               my->_unspent_outputs[output_reference( trx.id(), out_idx )] = trx.outputs[out_idx];
                            else
                            {
                               mark_as_spent( output_reference(trx.id(), out_idx ) );
                               my->_spent_outputs[output_reference( trx.id(), out_idx )] = trx.outputs[out_idx];
                            }
                            std::cerr<<"found block["<<i<<"].trx["<<trx_idx<<"].output["<<out_idx<<"]  " << std::string(trx.id()) <<" => "<<std::string(owner)<<"\n";
                        }
                        else
                        {
                            std::cerr<<"skip block["<<i<<"].trx["<<trx_idx<<"].output["<<out_idx<<"] => "<<std::string(owner)<<"\n";
                        }
                        break;
                     }
                  }
              }
          }
       }
   } FC_RETHROW_EXCEPTIONS( warn, "" ) }

   void wallet::dump()
   {
       std::cerr<<"===========================================================\n";
       std::cerr<<"Unspent Outputs: \n";
       for( auto itr = my->_unspent_outputs.begin(); itr != my->_unspent_outputs.end(); ++itr )
       {
           std::cerr<<std::string(itr->first.trx_hash)<<":"<<int(itr->first.output_idx)<<"]  ";
           std::cerr<<std::string(itr->second.get_amount())<<" ";
           std::cerr<<fc::variant(itr->second.claim_func).as_string()<<" ";

           switch( itr->second.claim_func )
           {
              case claim_by_signature:
                 std::cerr<< std::string(itr->second.as<claim_by_signature_output>().owner);
                 break;
              default:
                 std::cerr<<"??";
           }
           std::cerr<<"\n";
       }
       std::cerr<<"===========================================================\n";
   }
    
} } // namespace bts::blockchain
