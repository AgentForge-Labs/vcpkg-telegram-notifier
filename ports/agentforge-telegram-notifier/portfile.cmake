set(SOURCE_PATH "${CURRENT_PORT_DIR}/../../sdk/cpp")
vcpkg_cmake_configure(
  SOURCE_PATH "${SOURCE_PATH}"
  OPTIONS -DAGENTFORGE_TELEGRAM_NOTIFIER_BUILD_TESTS=OFF
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(
  PACKAGE_NAME AgentForgeTelegramNotifier
  CONFIG_PATH lib/cmake/AgentForgeTelegramNotifier
)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(INSTALL "${SOURCE_PATH}/LICENSE"
  DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
  RENAME copyright)
