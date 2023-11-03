include_directories(..)
if (WIN32)
    include_directories(../include)
endif (WIN32)

list(GET LIB_TARGETS 0 LIBRTAUDIO)

add_executable(playsaw playsaw.cpp)
target_link_libraries(playsaw ${LIBRTAUDIO} ${LINKLIBS})

#add_executable(recordplay recordplay.cpp)
#target_link_libraries(recordplay ${LIBRTAUDIO} ${LINKLIBS})

#add_executable(asioduplex asioduplex.cpp)
#target_link_libraries(asioduplex ${LIBRTAUDIO} ${LINKLIBS})

add_executable(audioprobe audioprobe.cpp)
target_link_libraries(audioprobe ${LIBRTAUDIO} ${LINKLIBS})

add_executable(audioevents audioevents.cpp)
target_link_libraries(audioevents ${LIBRTAUDIO} ${LINKLIBS})
