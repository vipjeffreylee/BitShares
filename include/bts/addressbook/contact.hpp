#pragma once
#include <bts/config.hpp>
#include <bts/extended_address.hpp>
#include <fc/io/enum_type.hpp>
#include <fc/time.hpp>

namespace bts { namespace addressbook {

  enum privacy_level
  {
      block_contact,
      secret_contact,
      public_contact
  };
  
  /**
   *  A contact can have many different properties that 
   *  can be attached to their contact card.  Properties
   *  are authenticated by one or more other identities and
   *  thus can be validated / vetted.
   */
  enum contact_property_type
  {
      null_property      = 0,
      name_property      = 1, /// first, last, ...
      email_property     = 2, /// email address
      address_property   = 3, /// physical mailing address
      group_property     = 4, /// groups this contact is a member of
      date_property      = 5, /// a labeled date (birth, death, marriage, etc)
      photo_property     = 6, /// used to attach pictures of the contact (license,etc)
      alias_property     = 7, /// other public keys belonging to the same person
  };

  struct property_signature
  {
      fc::time_point               date_signed;
      fc::time_point               date_expires;
      fc::ecc::compact_signature   sig;
  };

  struct contact_property
  {
      fc::unsigned_int                  type;
      std::string                       label;
      fc::optional<std::vector<char>>   icon_png;
      fc::optional<uint64_t>            nonce; /// used to do proof of work for publishing properties
      fc::variant                       data;

      /**
       *  @param contact_key - the contact property implies a link to a particular contact key
       *                       which isn't included in the property itself.  So it is provided
       *                       to calculate the digest used by signed contact property.
       */
      fc::uint256                       digest( const fc::ecc::public_key& contact_key )const;
  };

  struct signed_contact_property : public contact_property
  {
      std::vector<property_signature>  signatures;
  };

  struct name_property_data
  {
      static const contact_property_type type;

      std::string first_name;
      std::string middle_name;
      std::string last_name;
  };

  struct photo_property_data
  {
      static const contact_property_type type;

      std::string        format;
      std::vector<char>  photo;
  };

  
  /**
   *  Static information known about a contact that may
   *  be shared with other people.  This should not include
   *  things like the index in the wallet (which is different for 
   *  everyone).
   */
  struct contact 
  {
      contact()
      :dac_id_hash(0){};

      void set_dac_id( const std::string& name );

      /** @note this is the primary key that identifies a contact,
       *     it may not change without invalidating everything else
       *     associated with this contact.  
       */
      fc::ecc::public_key             public_key;

      /// @note should be kept consistant with dac_id_string
      mutable uint64_t                dac_id_hash;

      /// @note This is keyhoteeId. dac_id_hash is hash of keyhoteeId.
      std::string                     dac_id_string; 

      std::vector<contact_property>   properties;
   };

  struct wallet_identity : public contact
  { //DLNFIX can we derive wallet_identity from wallet_contact instead to avoid code duplication?
      wallet_identity() : mining_effort(0.2) {}
      std::string getFullName() const { return first_name + " " + last_name; }
      std::string getDisplayName() const
        {
        std::string display_name;
        bool hasAlias = first_name.empty() && last_name.empty();
        if (hasAlias)
          {
          display_name = getFullName();
          display_name += " <";
          }
        display_name += dac_id_string;
        if (hasAlias)
          display_name += '>';
        return display_name;
        }

      std::string          wallet_ident;      // used to generate the master public key for this identity
      float                mining_effort;
      std::string          first_name;     // kept locally, not shared
      std::string          last_name;     // kept locally, not shared
      std::vector<char>    private_icon_png;  // kept locally, not shared

  };

  /**
   *  Contains the private information about a given contact
   *  that is only required by my local wallet, this information
   *  should not be shared with others.
   */
  struct wallet_contact : public contact
  {
      wallet_contact() : wallet_index(WALLET_INVALID_INDEX), privacy_setting(secret_contact), next_send_trx_id(0) {}
      std::string getDisplayName() const
        {
        std::string display_name;
        bool hasAlias = !(first_name.empty() && last_name.empty());
        if (hasAlias)
          {
          display_name = getFullName();
          display_name += " <";
          }
        display_name += dac_id_string;
        if (hasAlias)
          display_name += '>';
        return display_name;
        }

      /** used to generate the extended private key for this contact */
      uint32_t                               wallet_index;
      fc::enum_type<uint8_t,privacy_level>   privacy_setting;
      std::string                            first_name;
      std::string                            last_name;
      std::vector<char>                      icon_png; 

      std::string                            notes;

      /**
       *  Incremented everytime a new trx to this user is created 
       */
      uint32_t                               next_send_trx_id;

      /**
       *  Addresses that this uesr has the private
       *  keys to.  This address is given to the
       *  contact who can use these keys to send us money.
       */
      extended_public_key                    send_trx_address;

      /** channels this contact is expected to be listening on */
      std::vector<uint16_t>                  bitchat_recv_channels; /// where this contact listens
      std::vector<uint16_t>                  bitchat_broadcast_channels; /// where contact broadcasts
      fc::ecc::private_key                   bitchat_recv_broadcast_key;
    private:
      std::string getFullName() const { return first_name + " " + last_name; }
  };


} } // bts::addressbook


FC_REFLECT_ENUM( bts::addressbook::privacy_level, 
    (block_contact)
    (secret_contact)
    (public_contact) 
)

FC_REFLECT_ENUM( bts::addressbook::contact_property_type,
    (null_property)
    (name_property)
    (email_property)
    (address_property)
    (group_property)
    (date_property)
    (photo_property)
    (alias_property)
)

FC_REFLECT( bts::addressbook::property_signature,
    (date_signed)
    (date_expires)
    (sig)
)

FC_REFLECT( bts::addressbook::contact_property,
    (type)
    (label)
    (icon_png)
    (data)
)

FC_REFLECT_DERIVED( bts::addressbook::signed_contact_property, (bts::addressbook::contact_property),
    (signatures)
)

FC_REFLECT( bts::addressbook::name_property_data,
    (first_name)
    (middle_name)
    (last_name)
)

FC_REFLECT( bts::addressbook::photo_property_data, (format)(photo))

FC_REFLECT( bts::addressbook::contact,
    (public_key)
    (dac_id_hash)
    (dac_id_string)
    (properties)
)

FC_REFLECT_DERIVED( bts::addressbook::wallet_contact, (bts::addressbook::contact),
     (wallet_index)
     (privacy_setting)
     (first_name)
     (last_name)
     (icon_png)
     (notes)
     (next_send_trx_id)
     (send_trx_address)
     (bitchat_recv_channels)
     (bitchat_broadcast_channels)
     (bitchat_recv_broadcast_key)
)
  
FC_REFLECT_DERIVED( bts::addressbook::wallet_identity, (bts::addressbook::contact),
     (wallet_ident)
     (mining_effort)
     (first_name)
     (last_name)
     (private_icon_png)
)
