QT += core gui widgets serialport xml

CONFIG += c++14
TARGET = IDRConfigurator
TEMPLATE = app
VERSION = 0.0.2

# exe file icon (only the .ico; all other resources stay external)
RC_FILE = src/resource/icon/druppc.rc

INCLUDEPATH += $$PWD/inc $$PWD/src ui
DEPENDPATH += $$PWD/inc $$PWD/src ui

# Intermediate artifacts stay next to the Makefile (script uses %TEMP%).
MOC_DIR     = moc
OBJECTS_DIR = obj
UI_DIR      = ui
RCC_DIR     = rcc

FORMS += \
    src/widget/main_window/main_window.ui \
    src/widget/connection_settings/connection_settings.ui \
    src/widget/about/about.ui \
    src/widget/config_page/config_page.ui \
    src/widget/config_page/pages/mcu_id_page.ui \
    src/widget/config_page/pages/rtc_page.ui \
    src/widget/config_page/pages/storage_page.ui \
    src/widget/config_page/pages/digital_page.ui \
    src/widget/config_page/pages/analog_page.ui \
    src/widget/config_page/pages/address_page.ui \
    src/widget/config_page/pages/role_page.ui \
    src/widget/config_page/pages/frequency_page.ui \
    src/widget/config_page/pages/modulation_page.ui \
    src/widget/config_page/pages/power_page.ui \
    src/widget/config_page/pages/task_table_page.ui \
    src/widget/config_page/pages/rs485_page.ui \
    src/widget/config_page/pages/counters_page.ui \
    src/widget/config_page/pages/rssi_page.ui \
    src/widget/config_page/pages/calibration_page.ui \
    src/widget/config_page/pages/register_page.ui

SOURCES += \
    src/main.cpp \
    src/widget/main_window/main_window.cpp \
    src/widget/connection_settings/connection_settings.cpp \
    src/widget/about/about.cpp \
    src/widget/config_page/config_page.cpp \
    src/widget/config_page/master_config_page.cpp \
    src/widget/config_page/slave_config_page.cpp \
    src/function/serial.cpp \
    src/function/thread.cpp

HEADERS += \
    inc/widget/main_window/main_window.h \
    inc/widget/connection_settings/connection_settings.h \
    inc/widget/about/about.h \
    inc/widget/config_page/config_page.h \
    inc/widget/config_page/master_config_page.h \
    inc/widget/config_page/slave_config_page.h \
    inc/function/serial.h \
    inc/function/thread.h

TRANSLATIONS += src/resource/i18n/druppc_zh_CN.ts
