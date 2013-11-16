set(FFMPEG_DIR ${CMAKE_SOURCE_DIR}/libs/ffmpeg)
message("ffmpeg dir ${FFMPEG_DIR}")

set(FFMPEG_INCLUDE_DIR ${FFMPEG_DIR})
set(FFMPEG_LIBDIRS
    ${FFMPEG_DIR}/libavformat
    ${FFMPEG_DIR}/libavcodec
    ${FFMPEG_DIR}/libavutil
    )
set(FFMPEG_LIBRARIES avformat avcodec avutil m pthread z)

include_directories(. ${FFMPEG_INCLUDE_DIR})
link_directories(${FFMPEG_LIBDIRS})

add_executable(ffmpeg_decode ffmpeg_decode.c)
target_link_libraries(ffmpeg_decode ${FFMPEG_LIBRARIES})
