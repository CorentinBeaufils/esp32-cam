# Simulateur : émetteur UDP (le « faux ESP32 ») + cadenceur.
add_library(simulator STATIC src/pacer.cpp src/emitter.cpp)
target_include_directories(simulator PUBLIC include)
target_link_libraries(simulator
  PUBLIC  common asio
  PRIVATE projet_warnings
)

add_executable(simulator_app main.cpp)
target_link_libraries(simulator_app PRIVATE simulator projet_warnings)
set_target_properties(simulator_app PROPERTIES OUTPUT_NAME simulator)
