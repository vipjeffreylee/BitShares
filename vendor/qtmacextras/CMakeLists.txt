set( CMAKE_INCLUDE_CURRENT_DIR ON)
set( CMAKE_AUTOMOC ON  )
find_package( Qt5Widgets )
find_package( Qt5Core )
find_package( Qt5Gui )
find_package( Qt5WebKitWidgets )
include_directories( ${CMAKE_CURRENT_BINARY_DIR} )
include_directories( ${Qt5Gui_DIR}/../../../include/QtGui/5.1.1/QtGui )
include_directories( ${Qt5Core_DIR}/../../../include/QtCore/5.1.1/QtCore )
include_directories( ${Qt5Widgets_INCLUDE_DIRS} )
include_directories( ${Qt5Core_INCLUDE_DIRS} )
SET( CMAKE_CXX_COMPILER /usr/bin/clang++ )
SET( CMAKE_CXX_FLAGS )

IF( NOT Qt5Widgets_DIR MATCHES Qt5Widgets_DIR-NOTFOUND )

set( sources
      src/macextras/qmacfunctions.mm
      src/macextras/qmacfunctions_mac.mm
      src/macextras/qmactoolbardelegate.mm
      src/macextras/qmactoolbutton.mm
      src/macextras/qnstoolbar.mm
      src/macextras/qmacnativetoolbar.mm
   )


add_library( qtmacextras ${sources} )
target_link_libraries( qtmacextras Qt5::Gui )

ENDIF( NOT Qt5Widgets_DIR MATCHES Qt5Widgets_DIR-NOTFOUND )

