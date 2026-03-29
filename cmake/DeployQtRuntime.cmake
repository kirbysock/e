if(NOT DEFINED QT_BIN_DIR OR NOT DEFINED QT_PLUGIN_DIR OR NOT DEFINED APP_DIR)
    message(FATAL_ERROR "QT_BIN_DIR, QT_PLUGIN_DIR, and APP_DIR must be provided.")
endif()

file(MAKE_DIRECTORY "${APP_DIR}")
file(MAKE_DIRECTORY "${APP_DIR}/platforms")
file(MAKE_DIRECTORY "${APP_DIR}/sqldrivers")

file(GLOB QT_RUNTIME_DLLS "${QT_BIN_DIR}/*.dll")

if(QT_RUNTIME_DLLS)
    file(COPY ${QT_RUNTIME_DLLS} DESTINATION "${APP_DIR}")
endif()

if(EXISTS "${QT_PLUGIN_DIR}/platforms/qwindows.dll")
    file(COPY "${QT_PLUGIN_DIR}/platforms/qwindows.dll" DESTINATION "${APP_DIR}/platforms")
endif()

if(EXISTS "${QT_PLUGIN_DIR}/sqldrivers/qsqlite.dll")
    file(COPY "${QT_PLUGIN_DIR}/sqldrivers/qsqlite.dll" DESTINATION "${APP_DIR}/sqldrivers")
endif()
