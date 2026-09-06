# viewer_qt: Qt dashboard (video + stats + live chart).
#
# Built only when Qt6 (Widgets + Charts) AND OpenCV are present -- like the
# 'viewer' / 'viewer_up' targets, so the project still configures without a GUI.
find_package(Qt6 QUIET COMPONENTS Widgets Charts)
find_package(OpenCV QUIET)

if(Qt6_FOUND AND OpenCV_FOUND)
  # AUTOMOC: generates the meta-object code for the Q_OBJECT classes
  # (StreamController, MainWindow). Must be enabled BEFORE add_executable.
  set(CMAKE_AUTOMOC ON)

  # NB: the Q_OBJECT headers are listed in the target too. Without this, AUTOMOC
  # does not pair them (it only auto-detects a header when a .cpp with the SAME
  # name sits in the SAME directory; here include/qtv/*.hpp vs src/*.cpp -> miss).
  # Listing them forces moc to generate their vtables and signal code.
  add_executable(viewer_qt
    main.cpp
    src/stream_controller.cpp
    src/main_window.cpp
    src/to_qimage.cpp
    include/qtv/stream_controller.hpp
    include/qtv/main_window.hpp)

  target_include_directories(viewer_qt PRIVATE include ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(viewer_qt PRIVATE
    receiver upscale common          # our libs: network, upscaling, protocol
    Qt6::Widgets Qt6::Charts
    ${OpenCV_LIBS}
    projet_warnings)

  message(STATUS "Qt6 ${Qt6_VERSION} + OpenCV ${OpenCV_VERSION} -> target 'viewer_qt' enabled")
else()
  message(STATUS "Qt6 (Widgets+Charts) or OpenCV missing -> target 'viewer_qt' disabled "
                 "(install: qt6-base-dev qt6-charts-dev libopencv-dev).")
endif()
