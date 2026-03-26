add_subdirectory(core)
add_subdirectory(db)
add_subdirectory(media)
add_subdirectory(auth)
add_subdirectory(config)
add_subdirectory(api)
add_subdirectory(server)
add_subdirectory(service)

# Plugin subdirectories — only build as standalone DLLs when explicitly enabled
# By default, plugin sources are compiled into hub32api-server
if(BUILD_PLUGINS AND BUILD_PLUGINS_STANDALONE)
    add_subdirectory(plugins)
endif()
