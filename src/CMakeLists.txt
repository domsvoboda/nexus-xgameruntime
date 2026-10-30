# ------------------------------------------------------------
# xgameruntime-impl
# ------------------------------------------------------------

add_library(xgameruntime-impl STATIC
        main.cpp
        ${IDL_GENERATED_FILES} )

add_dependencies(xgameruntime-impl
        xgameruntime-idl )

# System
target_sources(xgameruntime-impl PRIVATE
        System/XGame.cpp
        System/XNetworking.cpp
        System/XAccessibility.cpp
        System/XAppCapture.cpp
        System/XGameActivation.cpp
        System/XGameEvent.cpp
        System/XGameInvite.cpp
        System/XGameProtocol.cpp
        System/XGameStreaming.cpp
        System/XGameUi.cpp
        System/GameBridge/Bridge.cpp
        System/GameInvite/Bridge.cpp
        System/GameInvite/Events.cpp
        System/GameInvite/Network.cpp
        System/GameInvite/Ui.cpp
        System/XGameRuntimeFeature.cpp
        System/XDisplay.cpp
        System/XError.cpp
        System/XStore.cpp
        System/XPackage.cpp
        System/XGameSave.cpp
        System/XUser.cpp
        System/User/Presence.cpp
        HttpClient.cpp
        System/XThreading.cpp
        System/XSystem.cpp
        System/PersistentStorage.cpp
        Runtime/Xml.cpp
        Runtime/Configuration.cpp
        Runtime/Shutdown.cpp )

# System/Threading (based on libHttpClient)
target_sources(xgameruntime-impl PRIVATE
        System/Threading/ThreadPool.cpp
        System/Threading/WaitTimer.cpp
        System/Threading/XAsync.cpp
        System/Threading/XTaskQueue.cpp
        System/Threading/QueueHandles.cpp )

target_link_libraries( xgameruntime-impl PRIVATE
        xgameruntime-common nexus-xml2 shell32 bcrypt advapi32 crypt32 runtimeobject ole32 winhttp wininet ws2_32 iphlpapi )

target_include_directories(xgameruntime-impl
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${IDL_BINARY_DIR} )

target_compile_features(xgameruntime-impl
        PUBLIC
        cxx_std_23 )

set(XGAMERUNTIME_EXPORTS xgameruntime.def)
if(MINGW AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(XGAMERUNTIME_EXPORTS xgameruntime-x86.def)
endif()

add_library(xgameruntime SHARED
        ${XGAMERUNTIME_EXPORTS}
        ${IDL_GENERATED_FILES} )

if(WIN32)
    set_target_properties(xgameruntime PROPERTIES
            PREFIX "" )
endif()

target_link_libraries( xgameruntime PUBLIC
        xgameruntime-impl )

set_target_properties(xgameruntime PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LINKER_LANGUAGE CXX )

target_link_options(xgameruntime PRIVATE
        -static
        -static-libgcc
        -static-libstdc++ )

include( GNUInstallDirs )
include( ${PROJECT_SOURCE_DIR}/cmake/Builtin.cmake )

# Proton supplies its architecture-specific Wine PE directory explicitly.
install( TARGETS xgameruntime
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} )
