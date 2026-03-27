target_sources(hub32api-core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/DatabaseManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SchoolRepository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LocationRepository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ComputerRepository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TeacherRepository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TeacherLocationRepository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TenantRepository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ClassRepository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StudentRepository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PendingRequestRepository.cpp
)
