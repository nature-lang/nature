Name: ${PROJECT_NAME}
Description: The cmocka unit testing library
Version: ${PROJECT_VERSION}
Libs: -L${CMAKE_INSTALL_FULL_LIBDIR} -lcmocka
Cflags: -I${CMAKE_INSTALL_FULL_INCLUDEDIR}
