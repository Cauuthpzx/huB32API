target_sources(hub32api-core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/MockSfuBackend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RoomManager.cpp
)

if(HUB32_WITH_MEDIASOUP)
    target_sources(hub32api-core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/MediasoupSfuBackend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/WorkerChannel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/WorkerMessageBuilder.cpp
    )
    target_compile_definitions(hub32api-core PRIVATE HUB32_WITH_MEDIASOUP)

    # FlatBuffers generated headers (copied from third_party/mediasoup/worker/fbs/FBS/)
    target_include_directories(hub32api-core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # FlatBuffers runtime headers
    find_package(Flatbuffers QUIET)
    if(Flatbuffers_FOUND)
        target_link_libraries(hub32api-core PRIVATE flatbuffers::flatbuffers)
    else()
        # Fallback: use headers from mediasoup-server-ref bundled copy
        target_include_directories(hub32api-core PRIVATE
            ${CMAKE_SOURCE_DIR}/third_party/mediasoup-server-ref/deps/flatbuffers/include
        )
    endif()

    # Link mediasoup worker library (Linux only)
    if(NOT WIN32)
        find_package(MediasoupWorker QUIET)
        if(MediasoupWorker_FOUND)
            target_link_libraries(hub32api-core PRIVATE mediasoup::worker)
        else()
            message(WARNING "mediasoup-worker not found. "
                    "Build: cd third_party/mediasoup/worker && meson setup build && ninja -C build")
        endif()
    endif()
endif()
