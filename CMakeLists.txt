cmake_minimum_required(VERSION 3.19)
project(BankManagementSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Widgets Test)

option(BANK_ENABLE_ENCRYPTION "Enable AES-256-GCM file encryption" ON)
set(BANK_OPENSSL_AVAILABLE OFF)
if(BANK_ENABLE_ENCRYPTION)
    find_package(OpenSSL QUIET COMPONENTS Crypto)
    if(TARGET OpenSSL::Crypto)
        set(BANK_OPENSSL_AVAILABLE ON)
        message(STATUS "Bank storage: OpenSSL Crypto found; AES-256-GCM enabled")
    else()
        message(WARNING
            "Bank storage: OpenSSL Crypto not found; using separate plain JSON compatibility files")
    endif()
else()
    message(STATUS
        "Bank storage: encryption explicitly disabled; using separate plain JSON compatibility files")
endif()

qt_standard_project_setup()

qt_add_library(BankDomain STATIC
    models/auditrecord.cpp
    models/auditrecord.h
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

if(BANK_OPENSSL_AVAILABLE)
    target_link_libraries(BankSecurity PUBLIC OpenSSL::Crypto)
    target_compile_definitions(BankSecurity PUBLIC BANK_HAS_OPENSSL=1)
endif()

# DataCodec 只依赖 SecurityUtils 的成熟密码学封装，业务层接口保持不变。
target_link_libraries(BankPersistence PUBLIC BankSecurity)

qt_add_library(BankAudit STATIC
    audit/auditlogger.cpp
    audit/auditlogger.h
)

target_include_directories(BankAudit
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(BankAudit
    PUBLIC
        BankDomain
        BankPersistence
        Qt::Core
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
        BankAudit
        BankPersistence
        BankSecurity
        Qt::Core
)

qt_add_library(BankUi STATIC
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    ui/depositdialog.cpp
    ui/depositdialog.h
    ui/depositdialog.ui
    ui/passworddialog.cpp
    ui/passworddialog.h
    ui/passworddialog.ui
    ui/profiledialog.cpp
    ui/profiledialog.h
    ui/profiledialog.ui
    ui/unfreezedialog.cpp
    ui/unfreezedialog.h
    ui/unfreezedialog.ui
    ui/withdrawdialog.cpp
    ui/withdrawdialog.h
    ui/withdrawdialog.ui
)

target_include_directories(BankUi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(BankUi
    PUBLIC
        BankDomain
        BankAudit
        BankPersistence
        BankSecurity
        BankServices
        Qt::Core
        Qt::Gui
        Qt::Widgets
)

qt_add_executable(BankManagementSystem
    WIN32 MACOSX_BUNDLE
    main.cpp
)

target_link_libraries(BankManagementSystem
    PRIVATE
        BankUi
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

qt_add_executable(BankAuditTests
    tests/tst_audit.cpp
)

set_target_properties(BankAuditTests PROPERTIES
    MACOSX_BUNDLE FALSE
)

target_link_libraries(BankAuditTests
    PRIVATE
        BankAudit
        Qt::Test
)

add_test(NAME BankAuditTests COMMAND BankAuditTests)

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

qt_add_executable(BankUiTests
    tests/tst_ui.cpp
)

set_target_properties(BankUiTests PROPERTIES
    MACOSX_BUNDLE FALSE
)

target_link_libraries(BankUiTests
    PRIVATE
        BankUi
        Qt::Test
)

add_test(NAME BankUiTests COMMAND BankUiTests)
set_tests_properties(BankUiTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

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
