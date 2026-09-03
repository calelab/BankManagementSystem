cmake_minimum_required(VERSION 3.19)
project(BankManagementSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Widgets Test)

qt_standard_project_setup()

qt_add_library(BankDomain STATIC
    models/bankstate.cpp
    models/bankstate.h
    models/banktypes.cpp
    models/banktypes.h
    models/depositor.cpp
    models/depositor.h
    models/fixeddeposit.cpp
    models/fixeddeposit.h
    models/transaction.cpp
    models/transaction.h
    services/interestcalculator.cpp
    services/interestcalculator.h
    utils/moneyutils.cpp
    utils/moneyutils.h
)

target_include_directories(BankDomain
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(BankDomain
    PUBLIC
        Qt::Core
)

qt_add_library(BankPersistence STATIC
    persistence/bankstatejsonserializer.cpp
    persistence/bankstatejsonserializer.h
    persistence/datacodec.cpp
    persistence/datacodec.h
    persistence/employeefilemanager.cpp
    persistence/employeefilemanager.h
    persistence/filemanager.cpp
    persistence/filemanager.h
)

target_include_directories(BankPersistence
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(BankPersistence
    PUBLIC
        BankDomain
        Qt::Core
)

qt_add_library(BankSecurity STATIC
    security/securityutils.cpp
    security/securityutils.h
)

target_include_directories(BankSecurity
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(BankSecurity
    PUBLIC
        BankDomain
        Qt::Core
        Qt::Network
)

qt_add_library(BankServices STATIC
    services/bankservice.cpp
    services/bankservice.h
)

target_include_directories(BankServices
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(BankServices
    PUBLIC
        BankDomain
        BankPersistence
        BankSecurity
        Qt::Core
)

qt_add_executable(BankManagementSystem
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
)

target_link_libraries(BankManagementSystem
    PRIVATE
        BankDomain
        BankPersistence
        BankSecurity
        BankServices
        Qt::Core
        Qt::Widgets
)

enable_testing()

qt_add_executable(BankDomainTests
    tests/tst_domain.cpp
)

set_target_properties(BankDomainTests PROPERTIES
    MACOSX_BUNDLE FALSE
)

target_link_libraries(BankDomainTests
    PRIVATE
        BankDomain
        Qt::Test
)

add_test(NAME BankDomainTests COMMAND BankDomainTests)

qt_add_executable(BankPersistenceTests
    tests/tst_persistence.cpp
)

set_target_properties(BankPersistenceTests PROPERTIES
    MACOSX_BUNDLE FALSE
)

target_link_libraries(BankPersistenceTests
    PRIVATE
        BankPersistence
        Qt::Test
)

add_test(NAME BankPersistenceTests COMMAND BankPersistenceTests)

qt_add_executable(BankServiceTests
    tests/tst_bankservice.cpp
)

set_target_properties(BankServiceTests PROPERTIES
    MACOSX_BUNDLE FALSE
)

target_link_libraries(BankServiceTests
    PRIVATE
        BankServices
        Qt::Test
)

add_test(NAME BankServiceTests COMMAND BankServiceTests)

install(TARGETS BankManagementSystem
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET BankManagementSystem
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
