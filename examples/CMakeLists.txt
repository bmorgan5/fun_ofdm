########################################################################
# Examples CMakeLists.txt
#
#
########################################################################

########################################################################
# Include directories so cmake knows where .h files live
########################################################################
include_directories (${PROJECT_SOURCE_DIR}/src)

########################################################################
# Example Sources
########################################################################
list(APPEND test_sim_srcs
        test_sim.cpp
)

list(APPEND test_tx_srcs
	test_tx.cpp
)

list(APPEND test_rx_srcs
        test_rx.cpp
)

list(APPEND test_transceiver_srcs
		simple_transceiver.cpp
)

########################################################################
# Create executables
########################################################################
add_executable(sim ${test_sim_srcs})
add_executable(test_tx ${test_tx_srcs})
add_executable(test_rx ${test_rx_srcs})
add_executable(transceiver ${test_transceiver_srcs})


########################################################################
# Link executables to fun_ofdm library
########################################################################
target_link_libraries(test_tx fun_ofdm)
target_link_libraries(test_rx fun_ofdm)
target_link_libraries(sim fun_ofdm)
target_link_libraries(transceiver fun_ofdm)

