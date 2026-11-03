# itds/biradx-itds.pro — BIRADx-H7 on the ITDS computer (the Jetson, Linux, Qt 5.9 or later).
#
#   cd itds && qmake biradx-itds.pro && make -j$(nproc)
#
# The application (app/), the screens (gui/), the fonts and the decision core (birxcore/) are the
# BIRADx-H7 source, compiled as they are with BIRADX_ITDS defined. Qt is used for the window and
# its input only: no QML, no widgets.

TEMPLATE = app
TARGET = biradx-itds
QT = core gui
CONFIG += release warn_on
CONFIG -= app_bundle

# C++17 on every Qt this may meet. Qt 5.9's qmake knows "c++1z" and not "c++17", and adds its
# own -std= for c++11 / c++14; whichever it picks, it is C++17 here.
CONFIG += c++1z
QMAKE_CXXFLAGS_CXX11    = -std=gnu++1z
QMAKE_CXXFLAGS_CXX14    = -std=gnu++1z
QMAKE_CXXFLAGS_CXX1Z    = -std=gnu++1z
QMAKE_CXXFLAGS_GNUCXX11 = -std=gnu++1z
QMAKE_CXXFLAGS_GNUCXX14 = -std=gnu++1z
QMAKE_CXXFLAGS_GNUCXX1Z = -std=gnu++1z

DEFINES += BIRADX_ITDS
INCLUDEPATH += .. ../birxcore ../app ../gui ../fonts

SOURCES += \
    main_itds.cpp \
    platform_itds.cpp \
    $$files(../birxcore/*.cpp) \
    $$files(../app/*.cpp) \
    $$files(../gui/*.cpp) \
    ../fonts/fonts_data.cpp

HEADERS += \
    platform_itds.h \
    $$files(../birxcore/*.h) \
    $$files(../app/*.h) \
    $$files(../gui/*.h) \
    ../fonts/fonts.h

LIBS += -lpthread

OBJECTS_DIR = build
MOC_DIR = build
RCC_DIR = build
