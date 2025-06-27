cmake_minimum_required(VERSION 3.12)

project(tools)

# Database migration tool
add_executable(db_migration db_migration.cpp)

target_link_libraries(db_migration
    csdb
    lib
)

set_property(TARGET db_migration PROPERTY CXX_STANDARD 17)
set_property(TARGET db_migration PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

# Install the migration tool
install(TARGETS db_migration
    RUNTIME DESTINATION bin
)