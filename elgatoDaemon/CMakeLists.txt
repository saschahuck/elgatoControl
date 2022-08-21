set(DAEMON_SOURCES
        main.cpp AvahiBrowser.cpp Log.cpp ElgatoLight.cpp HTTPRequest.hpp)

set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Avahi REQUIRED)
find_package(Threads REQUIRED)
find_package(nlohmann_json 3.10.5 REQUIRED)
find_package(fmt REQUIRED)

add_executable(elgatoDaemon ${DAEMON_SOURCES})
target_link_libraries(elgatoDaemon Avahi::client Threads::Threads nlohmann_json::nlohmann_json fmt::fmt)