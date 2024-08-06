include_directories(
    ${PULSE_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

set(pavucontrol-qt_HDRS
    pavucontrol.h
    mainwindow.h
    cardwidget.h
    channel.h
    devicewidget.h
    minimalstreamwidget.h
    rolewidget.h
    sinkinputwidget.h
    sinkwidget.h
    sourceoutputwidget.h
    sourcewidget.h
    streamwidget.h
    elidinglabel.h
)

set(pavucontrol-qt_SRCS
    pavucontrol.cc
    mainwindow.cc
    cardwidget.cc
    channel.cc
    devicewidget.cc
    minimalstreamwidget.cc
    rolewidget.cc
    sinkinputwidget.cc
    sinkwidget.cc
    sourceoutputwidget.cc
    sourcewidget.cc
    streamwidget.cc
    elidinglabel.cc
)

if (APPLE)
    if (KF5IconThemes_FOUND)
        ecm_add_app_icon(pavucontrol-qt_SRCS ICONS ${CMAKE_CURRENT_SOURCE_DIR}/resources/multimedia-volume-control.svgz)
    else()
        set(pavucontrol-qt_SRCS ${pavucontrol-qt_SRCS}
            resources/multimedia-volume-control.icns
        )
        set_source_files_properties(resources/multimedia-volume-control.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()
endif()

set(pavucontrol-qt_UI
    cardwidget.ui
    devicewidget.ui
    mainwindow.ui
    streamwidget.ui
)

# Translations
lxqt_translate_ts(pavucontrol-qt_QM_FILES
    UPDATE_TRANSLATIONS
        ${UPDATE_TRANSLATIONS}
    SOURCES
        ${pavucontrol-qt_HDRS}
        ${pavucontrol-qt_SRCS}
        ${pavucontrol-qt_UI}
    INSTALL_DIR
        "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/translations"
)

lxqt_translate_desktop(DESKTOP_FILES
    SOURCES
    pavucontrol-qt.desktop.in
    USE_YAML
)

if (APPLE)
    set_property(SOURCE
        pavucontrol.cc
        mainwindow.cc
        APPEND PROPERTY COMPILE_DEFINITIONS USE_THREADED_GLLOOP)
endif()

add_executable(pavucontrol-qt
    ${pavucontrol-qt_SRCS}
    ${pavucontrol-qt_QM_FILES}
    ${pavucontrol-qt_QM_LOADER}
    ${DESKTOP_FILES}
)
set_property(
     TARGET pavucontrol-qt APPEND
     PROPERTY COMPILE_DEFINITIONS
     PAVUCONTROL_QT_DATA_DIR="${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME}"
)

set_target_properties(pavucontrol-qt PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.PulseAudio.pavucontrol"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_LONG_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    MACOSX_BUNDLE_COPYRIGHT "LGPL-2.1"
)
if (NOT KF5IconThemes_FOUND)
    set_target_properties(pavucontrol-qt PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "multimedia-volume-control.icns"
        RESOURCE "resources/multimedia-volume-control.icns"
    )
endif()

target_link_libraries(pavucontrol-qt
    Qt5::Widgets
    ${PULSE_LDFLAGS}
    ${GLIB_LDFLAGS}
)

install(TARGETS
    pavucontrol-qt
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION "${BUNDLE_INSTALL_DIR}"
    COMPONENT Runtime
)

install(FILES
    ${DESKTOP_FILES}
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
    COMPONENT Runtime
)
