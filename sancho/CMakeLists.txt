add_executable( sancho main.cpp )
target_link_libraries( sancho upnpc-static bshare fc leveldb ${BOOST_LIBRARIES} ${PLATFORM_SPECIFIC_LIBS} )
