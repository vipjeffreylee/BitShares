#include <bts/bitchat/bitchat_messages.hpp>

namespace bts { namespace bitchat {

const message_type inv_message::type;
const message_type get_priv_message::type;
const message_type get_inv_message::type;
const message_type cache_inv_message::type;
const message_type get_cache_priv_message::type;
const message_type get_cache_inv_message::type;
const message_type encrypted_message::type;
const message_type server_info_message::type;
const message_type client_info_message::type;

} } // bts::bitchat
