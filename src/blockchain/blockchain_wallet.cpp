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
              fc::path                                                   _wallet_dat;
              wallet_data                                                _data;
              asset                                                      _current_fee_rate;

              std::unordered_map<output_reference,trx_output>            _unspent_outputs;
              std::unordered_map<output_reference,trx_output>            _spent_outputs;

              // maps address to private key index
              std::unordered_map<bts::address,uint32_t>                  _my_addresses;
              std::unordered_map<transaction_id_type,signed_transaction> _id_to_signed_transaction;
      };
   } // namespace detail

   wallet::wallet()
   :my( new detail::wallet_impl() )
   {
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
      bts::address addr = new_key.get_public_key();
      my->_my_addresses[addr] = my->_data.extra_keys.size();
      my->_data.extra_keys.push_back(new_key);
      return  addr;
   }

   void                  wallet::set_fee_rate( const asset& pts_per_byte )
   {
      my->_current_fee_rate = pts_per_byte;
   }

   signed_transaction    wallet::transfer( const asset& amnt, const bts::address& to )
   {
       signed_transaction trx; 

       // TODO: make a set...
       std::vector<bts::address> req_sigs; 

       asset total;
       auto fee = my->_current_fee_rate * 1024*4; // just assume 4 kb per trx
       ilog( "fee: ${fee}", ("fee",fee) );
       asset req_in = amnt + fee;

       // TODO: factor this out into a helper method that will fetch the desired inputs for
       // any asset type.
       for( auto itr = my->_unspent_outputs.begin(); itr != my->_unspent_outputs.end(); ++itr )
       {
           if( itr->second.claim_func == claim_by_signature && itr->second.unit == amnt.unit )
           {
               trx.inputs.push_back( trx_input( itr->first ) );
               total += itr->second.get_amount();
               req_sigs.push_back( itr->second.as<claim_by_signature_output>().owner );

               if( total >= req_in )
               {
                  break;
               }
           }
       }

       // make sure there is sufficient funds
       FC_ASSERT( total >= req_in );
      
       trx.outputs.push_back( 
         trx_output( claim_by_signature_output( to ), amnt) );

       // TODO: estimate fee
       //   - estimating the fee should calculate teh total size of the transaction
       //     plus the estimated size of the required signatures.  The fee must be
       //     set so that 
       //req_in += fee  

       auto change = total - req_in;

       trx.outputs.push_back( 
         trx_output( claim_by_signature_output( get_new_address() ), change) );

       for( uint32_t i = 0; i < req_sigs.size(); ++i )
       {
          sign_transaction( trx, req_sigs[i] );
       }

       for( auto itr = trx.inputs.begin(); itr != trx.inputs.end(); ++itr )
       {
           mark_as_spent( itr->output_ref );
       }
       
       return trx;
   }
   void wallet::mark_as_spent( const output_reference& r )
   {
      auto itr = my->_unspent_outputs.find(r);
      if( itr == my->_unspent_outputs.end() )
      {
          return;
      }
      my->_spent_outputs[r] = itr->second;
      my->_unspent_outputs.erase(r);
   }

   void wallet::sign_transaction( signed_transaction& trx, const bts::address& addr )
   {
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
                            std::cerr<<"found block["<<i<<"].trx["<<trx_idx<<"].output["<<out_idx<<"] => "<<std::string(owner)<<"\n";
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
           std::cerr<<std::string(itr->first.trx_hash)<<":"<<itr->first.output_idx<<"]  ";
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
