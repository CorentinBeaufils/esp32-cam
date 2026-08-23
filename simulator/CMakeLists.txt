# TP-P1a : le simulateur (émetteur UDP).
#
#   -DSIM_USE_SOLUTION=ON  -> compile contre les corrigés (solution/), utile
#                             pour vérifier que ton environnement est sain avant
#                             de commencer, ou comparer après coup.
option(SIM_USE_SOLUTION "Compiler le simulateur contre les corrigés" OFF)

if(SIM_USE_SOLUTION)
  set(SIM_SOURCES solution/pacer.cpp solution/emitter.cpp)
else()
  set(SIM_SOURCES src/pacer.cpp src/emitter.cpp)
endif()

add_library(simulator STATIC ${SIM_SOURCES})
target_include_directories(simulator PUBLIC include)
target_link_libraries(simulator
  PUBLIC  common asio
  PRIVATE projet_warnings
)

# L'exécutable runnable du simulateur.
add_executable(simulator_app main.cpp)
target_link_libraries(simulator_app PRIVATE simulator projet_warnings)
set_target_properties(simulator_app PROPERTIES OUTPUT_NAME simulator)
