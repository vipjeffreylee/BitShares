add_executable( bts_wallet main.cpp )
target_link_libraries( bts_wallet  bshare fc leveldb ${BOOST_LIBRARIES} ${PLATFORM_SPECIFIC_LIBS} upnpc-static ${rt_library})
