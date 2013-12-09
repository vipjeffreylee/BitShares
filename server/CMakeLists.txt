add_executable( bitshares_server messages.cpp server.cpp )
target_link_libraries( bitshares_server upnpc-static bshare fc leveldb ${BOOST_LIBRARIES} ${PLATFORM_SPECIFIC_LIBS} upnpc-static)

add_executable( bitshares_client messages.cpp client.cpp )
target_link_libraries( bitshares_client upnpc-static bshare fc leveldb ${BOOST_LIBRARIES} ${PLATFORM_SPECIFIC_LIBS} upnpc-static)
