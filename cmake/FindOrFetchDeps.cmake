# FindOrFetchDeps.cmake
# Aggregator that includes individual dependency finders.

include(FetchContent)

include(deps/Findcpp-httplib)
include(deps/FindNlohmannJson)
include(deps/Findspdlog)
include(deps/FindJwt-cpp)
include(deps/FindSQLite3)
include(deps/FindGoogleTest)
