#pragma once
#include <bts/addressbook/contact.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/crypto/sha512.hpp>

#include <unordered_map>

namespace fc { class path; }

namespace bts { namespace addressbook {

  namespace detail { class addressbook_impl; }

  /**
   *  Provides indexes for effecient lookup of contacts
   *  and abstracts the storage of the addressbook on
   *  disk.
   */
  class addressbook 
  {
     public:
        addressbook();
        ~addressbook();

        void open( const fc::path& abook_dir, const fc::uint512& key );

        /**
         *  @return the contacts indexed by the profile ID we have assigned to them.
         */
        const std::unordered_map<uint32_t,wallet_contact>& get_contacts()const;

        fc::optional<wallet_contact> get_contact_by_dac_id( const std::string& bitname_label    )const;
        fc::optional<wallet_contact> get_contact_by_public_key(const fc::ecc::public_key& dac_id_key )const;
        fc::optional<wallet_contact> get_contact_by_full_name(const std::string& full_name )const;

        void store_contact(wallet_contact& contact_to_store );

     private:
        void add_contact_to_lookup_tables(wallet_contact& contact);
  
        std::unique_ptr<detail::addressbook_impl> my;
  };

  typedef std::shared_ptr<addressbook> addressbook_ptr;

} } // bts::addressbook
