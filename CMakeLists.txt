cmake_minimum_required(VERSION 3.20)

project(ClipDrag
    VERSION 1.0.0
    DESCRIPTION "A lightweight native Windows screenshot clipping tool"
    LANGUAGES CXX
)

add_executable(ClipDrag WIN32 ClipDrag.cpp)

if(MINGW)
    target_link_options(ClipDrag PRIVATE -municode)
endif()

target_compile_features(ClipDrag PRIVATE cxx_std_17)

target_compile_definitions(ClipDrag PRIVATE
    UNICODE
    _UNICODE
)

target_link_libraries(ClipDrag PRIVATE user32 gdi32)

if(MSVC)
    target_compile_options(ClipDrag PRIVATE
        /W4
        /EHsc
        $<$<CONFIG:Release>:/O2>
    )
    target_compile_definitions(ClipDrag PRIVATE
        $<$<CONFIG:Release>:NDEBUG>
    )
else()
    target_compile_options(ClipDrag PRIVATE -Wall -Wextra)
endif()
