
add_library(cooler cooler.cpp)
target_include_directories(cooler PRIVATE ${CSTONE_DIR} ${COOLING_DIR})
target_link_libraries(cooler PRIVATE grackle)
target_compile_definitions(cooler PUBLIC SPH_EXA_HAVE_GRACKLE)
target_compile_definitions(cooler PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(cooler PRIVATE CONFIG_BFLOAT_8=ON)