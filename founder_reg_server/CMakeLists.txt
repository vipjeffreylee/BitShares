
add_executable( name_reg_server main.cpp )
target_link_libraries( name_reg_server  upnpc-static bshare fc leveldb ${BOOST_LIBRARIES} ${PLATFORM_SPECIFIC_LIBS} upnpc-static ${rt_library})
