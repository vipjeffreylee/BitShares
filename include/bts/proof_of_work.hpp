#pragma once
#include <fc/crypto/sha256.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <fc/uint128.hpp>

namespace bts {

    /** typedef to the same size */
    typedef fc::ripemd160 pow_hash;

    pow_hash proof_of_work( const fc::sha256& in );

}

