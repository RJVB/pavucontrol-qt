cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)
# CMP0000: Call the cmake_minimum_required() command at the beginning of the top-level
# CMakeLists.txt file even before calling the project() command.
# The cmake_minimum_required(VERSION) command implicitly invokes the cmake_policy(VERSION)
# command to specify that the current project code is written for the given range of CMake
# versions.
project(pavucontrol-qt)

option(UPDATE_TRANSLATIONS "Update source translation translations/*.ts files" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

include(GNUInstallDirs)
include(FeatureSummary)

# Minimum Versions
set(GLIB_MINIMUM_VERSION "2.50.0")
# set(LXQTBT_MINIMUM_VERSION "0.13.0")
set(QT_MINIMUM_VERSION "5.6.3")

find_package(Qt5 ${QT_MINIMUM_VERSION} COMPONENTS Widgets LinguistTools REQUIRED)
# find_package(lxqt-build-tools ${LXQTBT_MINIMUM_VERSION} REQUIRED)
# we include our own copies of the lxqt-build-tools modules (only FindGLIB and the
# translation ones are really required for now). This makes us independent of the
# hardwired Qt version requireement in the upstream lxqt-build-modules.
set(LXQT_CMAKE_MODULES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(GLIB ${GLIB_MINIMUM_VERSION} REQUIRED)

set(PAVUCONTROLQT_MAJOR_VERSION 1)
set(PAVUCONTROLQT_MINOR_VERSION 4)
set(PAVUCONTROLQT_PATCH_VERSION 0)
set(PAVUCONTROLQT_VERSION ${PAVUCONTROLQT_MAJOR_VERSION}.${PAVUCONTROLQT_MINOR_VERSION}.${PAVUCONTROLQT_PATCH_VERSION})
add_definitions("-DPAVUCONTROLQT_VERSION=\"${PAVUCONTROLQT_VERSION}\"")

include(LXQtPreventInSourceBuilds)
include(LXQtCompilerSettings NO_POLICY_SCOPE)
include(LXQtTranslate)

if (APPLE)
    find_package(ECM ${KF5_DEP_VERSION} NO_MODULE)
    if (ECM_FOUND)
        list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
        find_package(KF5IconThemes 5.54.0 QUIET)
        if (KF5IconThemes_FOUND)
            set_package_properties(KF5IconThemes PROPERTIES
                DESCRIPTION "For the ksvg2icns utility")
            include(ECMAddAppIcon)
        endif()
    endif()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(
    PULSE REQUIRED
    libpulse>=5.0
    libpulse-mainloop-glib>=0.9.16
)

add_subdirectory(src)

feature_summary(WHAT ALL   FATAL_ON_MISSING_REQUIRED_PACKAGES)
