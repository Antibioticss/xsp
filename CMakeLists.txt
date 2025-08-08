cmake_minimum_required(VERSION 3.5)
project(xsp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_BUILD_TYPE Release)
# set(CMAKE_BUILD_TYPE Debug)

# add_compile_options(-fsanitize=address,undefined)
# add_link_options(-fsanitize=address,undefined)

include_directories(src)

add_executable(xsp 
	src/xsp.c
	src/cli.c
	src/anchored_memchr/anchored_memchr.c
)

find_package(Threads REQUIRED)
target_link_libraries(xsp PRIVATE Threads::Threads)

add_executable(bf 
	test/bf.c
	src/cli.c
)
target_link_libraries(bf PRIVATE Threads::Threads)

# add_executable(patch_test test/patch_test.c)
# add_executable(search_test test/search_test.c)

# enable_testing()
# add_test(patch_test patch_test)
# add_test(search_test patch_test)
