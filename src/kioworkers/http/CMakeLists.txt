include(ECMMarkNonGuiExecutable)

include(ConfigureChecks.cmake)
configure_file(config-kioworker-http.h.in ${CMAKE_CURRENT_BINARY_DIR}/config-kioworker-http.h )

if(GSSAPI_FOUND)
    set(HAVE_LIBGSSAPI 1)
    if(GSSAPI_FLAVOR STREQUAL "MIT")
        set(GSSAPI_MIT 1)
    else()
        set(GSSAPI_MIT 0)
    endif()

    include_directories( ${GSSAPI_INCS} )

else()
    set(HAVE_LIBGSSAPI 0)
    set(GSSAPI_MIT 0)
endif()

configure_file(config-gssapi.h.in ${CMAKE_CURRENT_BINARY_DIR}/config-gssapi.h )

########### next target ###############

add_executable(kio_http_cache_cleaner)
# Mark it as non-gui so we won't create an app bundle on Mac OS X
ecm_mark_nongui_executable(kio_http_cache_cleaner)

target_sources(kio_http_cache_cleaner PRIVATE
    http_cache_cleaner.cpp
)

target_link_libraries(kio_http_cache_cleaner
   Qt6::DBus
   Qt6::Network # QLocalSocket
   KF6::KIOCore # KProtocolManager
   KF6::I18n)

install(TARGETS kio_http_cache_cleaner DESTINATION ${KDE_INSTALL_LIBEXECDIR_KF} )

########### next target ###############

# kio/httpfilter/Makefile.am: httpfilter
kcoreaddons_add_plugin(kio_http INSTALL_NAMESPACE "kf6/kio")

target_sources(kio_http PRIVATE
   http.cpp
   httpauthentication.cpp
   httpfilter.cpp
   tcpworkerbase.cpp
   ksslsettings.cpp
   )

ecm_qt_export_logging_category(
    IDENTIFIER KIO_HTTP
    CATEGORY_NAME kf.kio.workers.http
    OLD_CATEGORY_NAMES kf5.kio.kio_http kf.kio.slaves.http
    DEFAULT_SEVERITY Warning
    DESCRIPTION "KIO HTTP worker (KIO)"
    EXPORT KIO
)

ecm_qt_export_logging_category(
    IDENTIFIER KIO_HTTP_AUTH
    CATEGORY_NAME kf.kio.workers.http.auth
    OLD_CATEGORY_NAMES kf5.kio.kio_http.auth kf.kio.slaves.http.auth
    DESCRIPTION "kio http auth (KIO)"
    EXPORT KIO
)

ecm_qt_export_logging_category(
    IDENTIFIER KIO_HTTP_FILTER
    CATEGORY_NAME kf.kio.workers.http.filter
    OLD_CATEGORY_NAMES kf5.kio.kio_http.filter kf.kio.slaves.http.filter
    DESCRIPTION "kio http filter (KIO)"
    EXPORT KIO
)

target_link_libraries(kio_http
   PRIVATE
   Qt6::DBus
   Qt6::Network # QLocalSocket etc.
   Qt6::Xml     # QDom
   KF6::ConfigCore
   KF6::KIOCore
   KF6::Archive
   KF6::CoreAddons
   KF6::I18n
)
if(GSSAPI_FOUND)
  target_link_libraries(kio_http PRIVATE ${GSSAPI_LIBS})
endif()
