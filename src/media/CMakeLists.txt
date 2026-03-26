target_sources(hub32api-core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/MockSfuBackend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RoomManager.cpp
)

if(HUB32_WITH_MEDIASOUP)
    target_sources(hub32api-core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/MediasoupSfuBackend.cpp
    )
    target_compile_definitions(hub32api-core PRIVATE HUB32_WITH_MEDIASOUP)
    # TODO: Find and link libmediasoup-worker when building on Linux
    # target_link_libraries(hub32api-core PRIVATE mediasoup::worker)
endif()
