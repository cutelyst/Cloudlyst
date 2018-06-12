file(GLOB_RECURSE Cloudlyst_SRCS *.cpp *.h)

set(Cloudlyst_SRCS
    ${Cloudlyst_SRCS}
    ${TEMPLATES_SRC}
)

# Create the application
add_library(Cloudlyst SHARED ${Cloudlyst_SRCS})

# Link to Cutelyst
target_link_libraries(Cloudlyst
    Cutelyst::Core
    Qt5::Core
    Qt5::Network
)

