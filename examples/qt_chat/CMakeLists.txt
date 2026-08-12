add_executable(sioxx_qt_chat main.cpp)
target_compile_definitions(sioxx_qt_chat PRIVATE QT_NO_KEYWORDS)

if(TARGET Qt6::Widgets)
  target_link_libraries(sioxx_qt_chat PRIVATE sioxx::sioxx Qt6::Widgets)
else()
  target_link_libraries(sioxx_qt_chat PRIVATE sioxx::sioxx Qt5::Widgets)
endif()

set_target_properties(sioxx_qt_chat PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
