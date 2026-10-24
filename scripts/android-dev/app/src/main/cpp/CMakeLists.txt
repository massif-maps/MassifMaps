cmake_minimum_required(VERSION 3.22.1)
project(massifdemo C)

# Only the mm_* types: the functions are looked up in the libmassif.so the app has loaded already.
add_library(massifdemo SHARED custom_layer_demo.c heatmap_layer.c)
target_include_directories(massifdemo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../all/native)
target_link_libraries(massifdemo GLESv3 log dl)
target_link_options(massifdemo PRIVATE -Wl,-z,max-page-size=16384)
