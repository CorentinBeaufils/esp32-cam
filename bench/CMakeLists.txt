# TP-P4 : banc de comparaison des recepteurs.
#
# La BIBLIOTHEQUE bench (RunReport) est PURE (aucun asio, aucun OpenCV) ->
# testable seule, exactement comme upscale/display. C'est le RELEVE COMMUN
# qu'emettront tous les recepteurs pour etre comparables.
#
#   -DBENCH_USE_SOLUTION=ON  -> compile la lib contre le corrige.
#
# A VENIR (fournis) : l'executable 'replayer' (rejoue une capture sur loopback,
# xN, a debit controle) et 'recv_baseline' (recvfrom bloquant, meme format CSV)
# comme etalons de comparaison face a ton recepteur asio.
option(BENCH_USE_SOLUTION "Compiler le bench contre le corrige" OFF)

if(BENCH_USE_SOLUTION)
  set(BENCH_SOURCES solution/run_report.cpp)
else()
  set(BENCH_SOURCES src/run_report.cpp)
endif()

add_library(bench STATIC ${BENCH_SOURCES})
target_include_directories(bench PUBLIC include)
target_link_libraries(bench PRIVATE projet_warnings)

# Etalons du banc : sockets POSIX purs (pas d'asio, pas d'OpenCV). Reutilisent
# le vrai protocole (common) et le releve commun (bench).
#   replayer      : generateur de charge synthetique reproductible (loopback).
#   recv_baseline : recepteur naif recvfrom bloquant, emet le CSV commun.
if(UNIX)
  add_executable(replayer replayer.cpp)
  target_link_libraries(replayer PRIVATE common projet_warnings)

  add_executable(recv_baseline recv_baseline.cpp)
  target_link_libraries(recv_baseline PRIVATE bench common projet_warnings)

  # recv_asio : TON recepteur asio instrumente pour le banc (meme CSV que
  # recv_baseline). Necessite la lib 'receiver' (C++20, coroutines, asio),
  # definie plus haut dans le projet -> les includes/le standard se propagent.
  add_executable(recv_asio recv_asio.cpp)
  target_link_libraries(recv_asio PRIVATE receiver bench common asio projet_warnings)

  # --- Banc MULTI-FLUX : ou l'async est cense payer (N flux concurrents). ---
  find_package(Threads REQUIRED)

  # recv_baseline_mt : un thread bloquant PAR socket (montee en charge naive).
  add_executable(recv_baseline_mt recv_baseline_mt.cpp)
  target_link_libraries(recv_baseline_mt PRIVATE bench common Threads::Threads projet_warnings)

  # recv_asio_mux : N Receiver multiplexes sur UN io_context / UN thread.
  add_executable(recv_asio_mux recv_asio_mux.cpp)
  target_link_libraries(recv_asio_mux PRIVATE receiver bench common asio projet_warnings)

  # recv_asio_pool : idem, mais l'io_context est tourne par un POOL de threads
  # (auto-dimensionne sur l'affinite). Le design "multithread asio" de prod.
  add_executable(recv_asio_pool recv_asio_pool.cpp)
  target_link_libraries(recv_asio_pool PRIVATE receiver bench common asio Threads::Threads projet_warnings)

  # recv_asio_shard : M io_context INDEPENDANTS (1/coeur), sockets reparties.
  # Le design async qui scale (un reactor par coeur, zero verrou partage).
  add_executable(recv_asio_shard recv_asio_shard.cpp)
  target_link_libraries(recv_asio_shard PRIVATE receiver bench common asio Threads::Threads projet_warnings)
else()
  message(STATUS "bench: etalons replayer/recv_baseline desactives (POSIX requis)")
endif()
