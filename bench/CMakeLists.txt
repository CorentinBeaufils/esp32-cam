# Banc de comparaison des recepteurs.
#
# La BIBLIOTHEQUE bench (RunReport) est PURE (aucun asio, aucun OpenCV) ->
# testable seule. C'est le RELEVE COMMUN qu'emettent tous les recepteurs pour
# etre comparables.
add_library(bench STATIC src/run_report.cpp)
target_include_directories(bench PUBLIC include)
target_link_libraries(bench PRIVATE projet_warnings)

# Etalons du banc : sockets POSIX purs (pas d'asio, pas d'OpenCV). Reutilisent
# le vrai protocole (common) et le releve commun (bench).
if(UNIX)
  add_executable(replayer replayer.cpp)
  target_link_libraries(replayer PRIVATE common projet_warnings)

  add_executable(recv_baseline recv_baseline.cpp)
  target_link_libraries(recv_baseline PRIVATE bench common projet_warnings)

  # recv_asio : le recepteur asio instrumente (meme CSV que recv_baseline).
  add_executable(recv_asio recv_asio.cpp)
  target_link_libraries(recv_asio PRIVATE receiver bench common asio projet_warnings)

  # --- Banc MULTI-FLUX ---
  find_package(Threads REQUIRED)

  # recv_baseline_mt : un thread bloquant PAR socket (montee en charge naive).
  add_executable(recv_baseline_mt recv_baseline_mt.cpp)
  target_link_libraries(recv_baseline_mt PRIVATE bench common Threads::Threads projet_warnings)

  # recv_asio_mux : N Receiver multiplexes sur UN io_context / UN thread.
  add_executable(recv_asio_mux recv_asio_mux.cpp)
  target_link_libraries(recv_asio_mux PRIVATE receiver bench common asio projet_warnings)

  # recv_asio_pool : 1 io_context tourne par un POOL de threads.
  add_executable(recv_asio_pool recv_asio_pool.cpp)
  target_link_libraries(recv_asio_pool PRIVATE receiver bench common asio Threads::Threads projet_warnings)

  # recv_asio_shard : M io_context INDEPENDANTS (1/coeur), sockets reparties.
  add_executable(recv_asio_shard recv_asio_shard.cpp)
  target_link_libraries(recv_asio_shard PRIVATE receiver bench common asio Threads::Threads projet_warnings)
else()
  message(STATUS "bench: etalons desactives (POSIX requis)")
endif()
