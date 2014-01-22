FIND_PATH(BDB_CXX_INCLUDE_DIR db.h
/usr/include/libdb5
/usr/include/db5
/usr/include/libdb4
/usr/include/db4
/usr/local/include/libdb5
/usr/local/include/db5
/usr/local/include/libdb4
/usr/local/include/db4
)

FIND_LIBRARY(BDB_CXX_LIBRARIES NAMES db_cxx
/usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Berkeley "Could not find Berkeley DB >= 4.1" BDB_CXX_INCLUDE_DIR BDB_CXX_LIBRARIES)
