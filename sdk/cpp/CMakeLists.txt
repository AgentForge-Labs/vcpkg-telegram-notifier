cmake_minimum_required(VERSION 3.20)
project(AgentForgeTelegramNotifier VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(agentforge_telegram_notifier src/telegram_notifier.cpp)
add_library(agentforge::telegram_notifier ALIAS agentforge_telegram_notifier)
target_compile_features(agentforge_telegram_notifier PUBLIC cxx_std_20)
target_include_directories(agentforge_telegram_notifier PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(agentforge_telegram_notifier PUBLIC
  CURL::libcurl
  nlohmann_json::nlohmann_json)
set_target_properties(agentforge_telegram_notifier PROPERTIES
  EXPORT_NAME telegram_notifier
  VERSION ${PROJECT_VERSION}
  SOVERSION 0)

option(AGENTFORGE_TELEGRAM_NOTIFIER_BUILD_TESTS "Build Telegram Notifier C++ package tests" OFF)
if(AGENTFORGE_TELEGRAM_NOTIFIER_BUILD_TESTS)
  enable_testing()
  add_executable(agentforge_telegram_notifier_conformance test/conformance.cpp)
  target_link_libraries(agentforge_telegram_notifier_conformance PRIVATE agentforge::telegram_notifier)
endif()

install(TARGETS agentforge_telegram_notifier EXPORT AgentForgeTelegramNotifierTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/AgentForgeTelegramNotifier)

include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/AgentForgeTelegramNotifierConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/AgentForgeTelegramNotifierConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AgentForgeTelegramNotifier)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/AgentForgeTelegramNotifierConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/AgentForgeTelegramNotifierConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/AgentForgeTelegramNotifierConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AgentForgeTelegramNotifier)
install(EXPORT AgentForgeTelegramNotifierTargets
  FILE AgentForgeTelegramNotifierTargets.cmake
  NAMESPACE agentforge::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AgentForgeTelegramNotifier)
