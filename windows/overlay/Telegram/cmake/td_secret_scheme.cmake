# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_secret_scheme OBJECT)
init_non_host_target(td_secret_scheme)
add_library(tdesktop::td_secret_scheme ALIAS td_secret_scheme)

find_package(Python3 REQUIRED)

set(secret_scheme_gen_dst ${CMAKE_CURRENT_BINARY_DIR}/secret_gen)
file(MAKE_DIRECTORY ${secret_scheme_gen_dst})

set(secret_scheme_timestamp ${secret_scheme_gen_dst}/secret_scheme.timestamp)
set(secret_scheme_files
    ${secret_scheme_gen_dst}/secret_scheme.cpp
    ${secret_scheme_gen_dst}/secret_scheme.h
)

add_custom_command(
OUTPUT
    ${secret_scheme_timestamp}
BYPRODUCTS
    ${secret_scheme_files}
COMMAND
    ${Python3_EXECUTABLE}
    ${src_loc}/codegen/scheme/codegen_secret_scheme.py
    -o${secret_scheme_gen_dst}/secret_scheme
    ${src_loc}/mtproto/scheme/secret_api.tl
COMMENT "Generating secret chat scheme"
DEPENDS
    ${src_loc}/codegen/scheme/codegen_secret_scheme.py
    ${submodules_loc}/lib_tl/tl/generate_tl.py
    ${src_loc}/mtproto/scheme/secret_api.tl
)

generate_target(
    td_secret_scheme
    secret_scheme
    ${secret_scheme_timestamp}
    "${secret_scheme_files}"
    ${secret_scheme_gen_dst})

nice_target_sources(td_secret_scheme ${src_loc}/mtproto/scheme
PRIVATE
    secret_api.tl
)

target_include_directories(td_secret_scheme
PUBLIC
    ${src_loc}
)

target_link_libraries(td_secret_scheme
PUBLIC
    desktop-app::lib_base
    desktop-app::lib_tl
)
