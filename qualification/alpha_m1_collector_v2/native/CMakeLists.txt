cmake_minimum_required(VERSION 3.20)
project(alpha_m1_native_verified_bootstrap LANGUAGES CXX)

find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
add_executable(alpha_m1_native_verified_bootstrap verified_bootstrap.cpp)
target_compile_features(alpha_m1_native_verified_bootstrap PRIVATE cxx_std_20)
target_compile_options(alpha_m1_native_verified_bootstrap PRIVATE
  -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror
)
target_link_libraries(alpha_m1_native_verified_bootstrap PRIVATE OpenSSL::Crypto)
