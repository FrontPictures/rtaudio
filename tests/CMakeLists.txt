include_directories(..)
if (WIN32)
    include_directories(../include)
endif (WIN32)

list(GET LIB_TARGETS 0 LIBRTAUDIO)

add_executable(playsaw playsaw.cpp)
target_link_libraries(playsaw ${LIBRTAUDIO} ${LINKLIBS})
