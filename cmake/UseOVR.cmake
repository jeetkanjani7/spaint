################
# UseOVR.cmake #
################

OPTION(WITH_OVR "Build with Oculus support?" OFF)

IF(WITH_OVR)
  IF(MSVC_IDE)
    FIND_PATH(OVR_INCLUDE_DIR OVR.h HINTS $ENV{HOMEPATH}/Downloads/OculusSDK/LibOVR/Include)
    FIND_LIBRARY(OVR_LIBRARY LibOVR HINTS $ENV{HOMEPATH}/Downloads/OculusSDK/LibOVR/Lib/Windows/x64/Release/VS2013)
  ELSEIF(APPLE)
    FIND_PATH(OVR_INCLUDE_DIR OVR.h HINTS ~/Downloads/OculusSDK/LibOVR/Include)
    FIND_LIBRARY(OVR_LIBRARY LibOVR HINTS ~/Downloads/OculusSDK/LibOVR/Lib/Mac/Release)
  ELSEIF("${CMAKE_SYSTEM}" MATCHES "Linux")
    FIND_PATH(OVR_INCLUDE_DIR OVR.h HINTS ~/software/ovr_sdk_linux_0.5.0.1/LibOVR/Include ~/software/ovr_sdk_linux_0.5.0.1/LibOVR/Include)
    FIND_LIBRARY(OVR_LIBRARY OVR HINTS ~/software/ovr_sdk_linux_0.5.0.1/LibOVR/Lib/Linux/x86_64/Debug ~/Software/ovr_sdk_linux_0.5.0.1/LibOVR/Lib/Linux/x86_64/Debug)
  ELSE()
    FIND_PATH(OVR_INCLUDE_DIR OVR.h)
    FIND_LIBRARY(OVR_LIBRARY OVR)
  ENDIF()

  IF(APPLE)
    FIND_LIBRARY(COCOA Cocoa)
    FIND_LIBRARY(COREFOUNDATION CoreFoundation)
    FIND_LIBRARY(COREGRAPHICS CoreGraphics)
    FIND_LIBRARY(IOKIT IOKit)
  ENDIF()

  IF("${CMAKE_SYSTEM}" MATCHES "Linux")
    FIND_PACKAGE(X11)
  ENDIF()

  INCLUDE_DIRECTORIES(${OVR_INCLUDE_DIR})
  ADD_DEFINITIONS(-DWITH_OVR)
ENDIF()
