# TP-P1b : le récepteur (UDP asynchrone + télémétrie). Coroutines -> C++20.
#
#   -DRECV_USE_SOLUTION=ON  -> compile contre les corrigés (solution/).
option(RECV_USE_SOLUTION "Compiler le recepteur contre les corriges" OFF)

if(RECV_USE_SOLUTION)
  set(RECV_SOURCES solution/metrics.cpp solution/receiver.cpp)
else()
  set(RECV_SOURCES src/metrics.cpp src/receiver.cpp)
endif()

add_library(receiver STATIC ${RECV_SOURCES})
target_include_directories(receiver PUBLIC include)
target_link_libraries(receiver
  PUBLIC  common asio
  PRIVATE projet_warnings
)
target_compile_features(receiver PUBLIC cxx_std_20)   # coroutines

add_executable(receiver_app main.cpp)
target_link_libraries(receiver_app PRIVATE receiver projet_warnings)
set_target_properties(receiver_app PROPERTIES OUTPUT_NAME receiver)
