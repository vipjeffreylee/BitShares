#pragma once
#include <fc/crypto/elliptic.hpp>
#include <string>
#include <fc/filesystem.hpp>
#include <fc/reflect/reflect.hpp>

namespace bts { namespace bitchat {
class email_draft
{
   public:
      email_draft():draft_id(0){}

      uint32_t                                 draft_id;
      fc::optional<fc::ecc::public_key_data>   from;
      std::vector<fc::ecc::public_key_data>    to;
      std::vector<fc::ecc::public_key_data>    cc;
      std::vector<fc::ecc::public_key_data>    bcc;
      std::string                              subject;
      std::vector<fc::path>                    attachments;
      std::string                              body;
};

} }

FC_REFLECT( bts::bitchat::email_draft, (from)(to)(cc)(bcc)(subject)(attachments)(body) )
