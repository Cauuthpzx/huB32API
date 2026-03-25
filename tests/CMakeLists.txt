# GTest is already fetched by cmake/deps/FindGoogleTest.cmake via FindOrFetchDeps.
# Do not duplicate the FetchContent block here.

add_subdirectory(unit)
add_subdirectory(integration)
