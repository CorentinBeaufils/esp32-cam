# Receiver comparison benchmark.
#
# The bench LIBRARY (RunReport) is PURE (no asio, no OpenCV) -> testable on
# its own. It is the COMMON REPORT that all receivers emit so as to be
# comparable.
add_library(bench STATIC src/run_report.cpp)
target_include_directories(bench PUBLIC include)
target_link_libraries(bench PRIVATE projet_warnings)

# Benchmark references: pure POSIX sockets (no asio, no OpenCV). They reuse the
# real protocol (common) and the common report (bench).
if(UNIX)
  add_executable(replayer replayer.cpp)
  target_link_libraries(replayer PRIVATE common projet_warnings)

  add_executable(recv_baseline recv_baseline.cpp)
  target_link_libraries(recv_baseline PRIVATE bench common projet_warnings)

  # recv_asio: the instrumented asio receiver (same CSV as recv_baseline).
  add_executable(recv_asio recv_asio.cpp)
  target_link_libraries(recv_asio PRIVATE receiver bench common asio projet_warnings)

  # --- MULTI-STREAM benchmark ---
  find_package(Threads REQUIRED)

  # recv_baseline_mt: one blocking thread PER socket (naive scale-up).
  add_executable(recv_baseline_mt recv_baseline_mt.cpp)
  target_link_libraries(recv_baseline_mt PRIVATE bench common Threads::Threads projet_warnings)

  # recv_asio_mux: N Receivers multiplexed onto ONE io_context / ONE thread.
  add_executable(recv_asio_mux recv_asio_mux.cpp)
  target_link_libraries(recv_asio_mux PRIVATE receiver bench common asio projet_warnings)

  # recv_asio_pool: 1 io_context driven by a POOL of threads.
  add_executable(recv_asio_pool recv_asio_pool.cpp)
  target_link_libraries(recv_asio_pool PRIVATE receiver bench common asio Threads::Threads projet_warnings)

  # recv_asio_shard: M INDEPENDENT io_contexts (1/core), sockets spread across them.
  add_executable(recv_asio_shard recv_asio_shard.cpp)
  target_link_libraries(recv_asio_shard PRIVATE receiver bench common asio Threads::Threads projet_warnings)
else()
  message(STATUS "bench: baselines disabled (POSIX required)")
endif()
