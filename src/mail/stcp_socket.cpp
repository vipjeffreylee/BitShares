#include <mail/stcp_socket.hpp>
#include <fc/crypto/hex.hpp>
#include <fc/crypto/aes.hpp>
#include <fc/crypto/city.hpp>
#include <assert.h>
#include <fc/log/logger.hpp>
#include <fc/network/ip.hpp>
#include <fc/exception/exception.hpp>

namespace mail {

stcp_socket::stcp_socket()
:_buf_len(0)
{
}
stcp_socket::~stcp_socket()
{
}

void     stcp_socket::connect_to( const fc::ip::endpoint& ep )
{
    _sock.connect_to( ep );
    _priv_key = fc::ecc::private_key::generate();
    fc::ecc::public_key pub = _priv_key.get_public_key();
    auto s = pub.serialize();
    _sock.write( (char*)&s, sizeof(s) );
    fc::ecc::public_key_data rpub;
    _sock.read( (char*)&rpub, sizeof(rpub) );

    auto shared_secret = _priv_key.get_shared_secret( rpub );
//    ilog("shared secret ${s}", ("s", shared_secret) );
    _send_aes.init( fc::sha256::hash( (char*)&shared_secret, sizeof(shared_secret) ), 
                    fc::city_hash_crc_128((char*)&shared_secret,sizeof(shared_secret) ) );
    _recv_aes.init( fc::sha256::hash( (char*)&shared_secret, sizeof(shared_secret) ), 
                    fc::city_hash_crc_128((char*)&shared_secret,sizeof(shared_secret) ) );
}

/**
 *   This method must read at least 8 bytes at a time from
 *   the underlying TCP socket so that it can decrypt them. It
 *   will buffer any left-over.
 */
size_t   stcp_socket::readsome( char* buffer, size_t len )
{ try {
    assert( (len % 16) == 0 );
    assert( len >= 16 );
    char crypt_buf[4096];
    len = std::min<size_t>(sizeof(crypt_buf),len);

    size_t s = _sock.readsome( crypt_buf, len );
    if( s % 16 ) 
    {
        ilog( "s: %{s}", ("s",s) );
        _sock.read( crypt_buf + s, 16 - (s%16) );
        s += 16-(s%16);
    }
    _recv_aes.decode( crypt_buf, s, buffer );
    return s;
} FC_RETHROW_EXCEPTIONS( warn, "", ("len",len) ) }

bool     stcp_socket::eof()const
{
  return _sock.eof();
}

size_t   stcp_socket::writesome( const char* buffer, size_t len )
{ try {
    assert( len % 16 == 0 );
    assert( len > 0 );
    char crypt_buf[4096];
    len = std::min<size_t>(sizeof(crypt_buf),len);
    memcpy( crypt_buf, buffer, len );
    /**
     * every sizeof(crypt_buf) bytes the blowfish channel
     * has an error and doesn't decrypt properly...  disable
     * for now because we are going to upgrade to something
     * better.
     */
    //auto cipher_len =
    _send_aes.encode( buffer, len, crypt_buf );
    FC_ASSERT( len >= 16 );
    FC_ASSERT( len % 16 == 0);
   // memcpy( crypt_buf, buffer, len );
    _sock.write( (char*)crypt_buf, len );
    return len;
} FC_RETHROW_EXCEPTIONS( warn, "", ("len",len) ) }

void     stcp_socket::flush()
{
   _sock.flush();
}


void     stcp_socket::close()
{
  try 
  {
   _sock.close();
  }FC_RETHROW_EXCEPTIONS( warn, "error closing stcp socket" );
}

void    stcp_socket::accept()
{
    _priv_key = fc::ecc::private_key::generate();
    fc::ecc::public_key pub = _priv_key.get_public_key();
    auto s = pub.serialize();
    _sock.write( (char*)&s, sizeof(s) );
    fc::ecc::public_key_data rpub;
    _sock.read( (char*)&rpub, sizeof(rpub) );

    auto shared_secret = _priv_key.get_shared_secret( fc::ecc::public_key(rpub) );
//    ilog("shared secret ${s}", ("s", shared_secret) );
    _send_aes.init( fc::sha256::hash( (char*)&shared_secret, sizeof(shared_secret) ), 
                    fc::city_hash_crc_128((char*)&shared_secret,sizeof(shared_secret) ) );
    _recv_aes.init( fc::sha256::hash( (char*)&shared_secret, sizeof(shared_secret) ), 
                    fc::city_hash_crc_128((char*)&shared_secret,sizeof(shared_secret) ) );
}


} // namespace mail
