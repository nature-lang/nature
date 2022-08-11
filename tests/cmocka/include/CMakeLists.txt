project(cmocka-header C)

install(FILES
            cmocka.h
            cmocka_pbc.h
        DESTINATION
            ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT
            ${PROJECT_NAME})

if (WITH_CMOCKERY_SUPPORT)
    install(FILES
                cmockery/cmockery.h
                cmockery/pbc.h
            DESTINATION
                ${CMAKE_INSTALL_INCLUDEDIR}/cmockery
            COMPONENT
                ${PROJECT_NAME})
endif()
