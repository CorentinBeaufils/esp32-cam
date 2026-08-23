# Récepteur : réception UDP asynchrone (asio, coroutines -> C++20) + télémétrie.
add_library(receiver STATIC src/metrics.cpp src/receiver.cpp)
target_include_directories(receiver PUBLIC include)
target_link_libraries(receiver
  PUBLIC  common asio
  PRIVATE projet_warnings
)
target_compile_features(receiver PUBLIC cxx_std_20)   # coroutines

add_executable(receiver_app main.cpp)
target_link_libraries(receiver_app PRIVATE receiver projet_warnings)
set_target_properties(receiver_app PROPERTIES OUTPUT_NAME receiver)
