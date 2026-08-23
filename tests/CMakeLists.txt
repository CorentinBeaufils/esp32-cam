# TEST_PREFIX : préfixe le nom CTest de chaque test par son composant, pour que
# `ctest -R protocol|sim|recv|display` filtre comme on l'attend (sinon le filtre
# porte sur le nom Catch brut, ex. "latest: ...", et ne matche pas "display").

add_executable(test_protocol test_protocol.cpp)
target_link_libraries(test_protocol PRIVATE common Catch2::Catch2WithMain projet_warnings)
catch_discover_tests(test_protocol TEST_PREFIX "protocol::")

add_executable(test_simulator test_simulator.cpp)
target_link_libraries(test_simulator PRIVATE simulator common asio Catch2::Catch2WithMain projet_warnings)
catch_discover_tests(test_simulator TEST_PREFIX "sim::")

# test_receiver utilise le recepteur (C++20, coroutines) + l'emetteur.
add_executable(test_receiver test_receiver.cpp)
target_link_libraries(test_receiver PRIVATE receiver simulator common asio Catch2::Catch2WithMain projet_warnings)
catch_discover_tests(test_receiver TEST_PREFIX "recv::")

# test_display : LatestFrame (handoff thread-safe). Pas d'OpenCV.
add_executable(test_display test_display.cpp)
target_link_libraries(test_display PRIVATE display common Catch2::Catch2WithMain projet_warnings)
catch_discover_tests(test_display TEST_PREFIX "display::")

# test_upscaler : ScaleStats + UpscalePolicy (logique pure). Pas d'OpenCV.
add_executable(test_upscaler test_upscaler.cpp)
target_link_libraries(test_upscaler PRIVATE upscale Catch2::Catch2WithMain projet_warnings)
catch_discover_tests(test_upscaler TEST_PREFIX "upscale::")

# test_bench : RunReport (releve commun d'un run, logique pure). Pas d'OpenCV.
add_executable(test_bench test_bench.cpp)
target_link_libraries(test_bench PRIVATE bench Catch2::Catch2WithMain projet_warnings)
catch_discover_tests(test_bench TEST_PREFIX "bench::")
