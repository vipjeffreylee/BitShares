#pragma once
#include <QDateTime>
#include <QString>
#include <QIcon>
#include <fc/crypto/elliptic.hpp>

enum PrivacyLevel
{
    Blocked,
    Secret,
    Public
};

class Contact
{
   public:
      Contact():wallet_account_index(-1),privacy_level(Secret){}

      /// the account index used in our wallet.
      int32_t                    wallet_account_index;
      QIcon                      icon;
      QString                    first_name;
      QString                    last_name;
      QString                    company;
      QString                    bit_id;
      /// the public key currently associated with bit_id...
      fc::ecc::public_key_data   bit_id_public_key; 

      /// @note this is the key that identifies the contact,
      //  because this is how we first communicated with them,
      //  the bid_id and associated public key may change in which case
      //  the bit_id becomes invalid and we must alert the user that
      //  the bit_id no longer matches the key.
      fc::ecc::public_key_data   public_key;
      QDate                      known_since;
      PrivacyLevel               privacy_level;
      QString                    email_address;
      QString                    phone_number;
};