#pragma once
#include <bts/blockchain/block.hpp>
#include <bts/blockchain/transaction.hpp>
#include <fc/io/datastream.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>

using namespace bts::blockchain;

enum message_type
{
    version_msg,
    get_block_msg,
    get_transaction_msg,
    block_msg,
    trx_block_msg,
    transaction_msg
};

FC_REFLECT_ENUM( message_type, (version_msg)(get_block_msg)(get_transaction_msg)(block_msg)(trx_block_msg)(transaction_msg) )

struct version_message
{
    static const message_type type;
    version_message(uint32_t v=0):version(v){}

    uint32_t                  version;
};

/** Response is a trx_block_message */
struct get_block_message
{
    static const message_type type;
    get_block_message( uint32_t bn = 0 )
    :block_num(bn){}

    uint32_t block_num;
};

struct get_transaction_message
{
    static const message_type type;
    get_transaction_message(){}
    get_transaction_message( const transaction_id_type& id )
    :trx_id(id){}

    transaction_id_type trx_id;
};

struct block_message
{
    static const message_type type;
    block_message(){};
    block_message( const full_block& b )
    :blk(b){}

    full_block                   blk;
    fc::ecc::compact_signature   sig;
};


struct trx_block_message
{
    static const message_type type;
    trx_block_message(){};
    trx_block_message( const trx_block& b )
    :blk(b){}

    trx_block                    blk;
    fc::ecc::compact_signature   sig;
};

struct transaction_message
{
    static const message_type type;
    transaction_message(){}

    transaction_message( const signed_transaction& t )
    :trx(t){}

    signed_transaction trx;
};


FC_REFLECT( version_message,         (version)   )
FC_REFLECT( get_block_message,       (block_num) )
FC_REFLECT( get_transaction_message, (trx_id)    )
FC_REFLECT( block_message,           (blk)(sig)  )
FC_REFLECT( trx_block_message,       (blk)(sig)  )
FC_REFLECT( transaction_message,     (trx)       )


struct message
{
   public:
      fc::enum_type<uint16_t,message_type>  msg_type;
      std::vector<char>                     data;

      message(){}

      template<typename T>
      message( const T& m )
      {
         msg_type = T::type;
         data     = fc::raw::pack(m);
      }

      /**
       *  Automatically checks the type and deserializes T in the
       *  opposite process from the constructor.
       */
      template<typename T>
      T as()const 
      {
        try {
           FC_ASSERT( msg_type == T::type );
           T tmp;
           if( data.size() )
           {
              fc::datastream<const char*> ds( data.data(), data.size() );
              fc::raw::unpack( ds, tmp );
           }
           else
           {
              // just to make sure that tmp shouldn't have any data
              fc::datastream<const char*> ds( nullptr, 0 );
              fc::raw::unpack( ds, tmp );
           }
           return tmp;
        } FC_RETHROW_EXCEPTIONS( warn, 
             "error unpacking network message as a '${type}'  ${x} != ${msg_type}", 
             ("type", fc::get_typename<T>::name() )
             ("x", T::type)
             ("msg_type", msg_type)
             );
      }
};


FC_REFLECT( message, (msg_type)(data) );
