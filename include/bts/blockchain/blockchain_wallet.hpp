#pragma once
#include <bts/blockchain/blockchain_db.hpp>

namespace bts { namespace blockchain {

   namespace detail { class wallet_impl; }

   /**
    *  The wallet stores all signed_transactions that reference one of its
    *  addresses in the inputs or outputs section.  It also tracks all
    *  private keys, spend states, etc...
    */
   class wallet
   {
        public:
           wallet();
           ~wallet();

           void open( const fc::path& wallet_file );
           void save();

           bts::address          get_new_address();
           asset                 get_balance( asset::type t );
           void                  set_stake( uint64_t stake );
           void                  import_key( const fc::ecc::private_key& key );
           signed_transaction    transfer( const asset& amnt, const bts::address& to, const asset& fee );
           signed_transaction    bid( const asset& amnt, const price& ratio );
           signed_transaction    cancel_bid( const transaction_id_type& bid );
           signed_transaction    borrow( const asset& amnt );
           signed_transaction    cover( const asset& amnt );

           void sign_transaction( signed_transaction& trx, const bts::address& addr );
           void scan_chain( blockchain_db& chain, uint32_t from_block_num = 0 );
           void mark_as_spent( const output_reference& r );
           void dump();

        private:
           std::unique_ptr<detail::wallet_impl> my;
   };
} } // bts::blockchain
