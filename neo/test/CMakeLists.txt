include_directories(. ${FFMPEG_INCLUDE_DIR})
link_directories(${FFMPEG_LIBRARIES_DIRS})

add_executable(ffmpeg_decode ffmpeg_decode.c)
target_link_libraries(ffmpeg_decode ${FFMPEG_LIBRARIES} pthread)
