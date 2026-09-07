# FetchContent keeps the first declaration. Fill the two archive hash gaps in
# pinned Aurora without changing its source or dropping its SDL patch hooks.
include(FetchContent)
FetchContent_Declare(abseil-cpp
  URL https://github.com/abseil/abseil-cpp/archive/refs/tags/20240722.0.tar.gz
  URL_HASH SHA256=f50e5ac311a81382da7fa75b97310e4b9006474f9560ac46f54a9967f07d4ae3
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  EXCLUDE_FROM_ALL
)
FetchContent_Declare(SDL
  URL https://github.com/libsdl-org/SDL/archive/release-3.4.10.tar.gz
  URL_HASH SHA256=0dc11d980ba17250200718fa4e28011da293f27ed92f92203afffe396811f307
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  PATCH_COMMAND ${CMAKE_COMMAND}
    -DSDL_SOURCE_DIR=<SOURCE_DIR>
    -P "${PROJECT_SOURCE_DIR}/.deps/aurora/cmake/patches/apply-sdl3-android-nintendo-auto-mapping.cmake"
  COMMAND ${CMAKE_COMMAND}
    -DSDL_SOURCE_DIR=<SOURCE_DIR>
    -P "${PROJECT_SOURCE_DIR}/.deps/aurora/cmake/patches/apply-sdl3-android-security-exception.cmake"
  EXCLUDE_FROM_ALL
)
