add_library(
    definition
    structuredrichtext.cpp
    structuredrichtext_content.cpp
    structuredrichtext_css.cpp
    structuredrichtext_layout.cpp
    structuredrichtext.h
    structuredrichtext_p.h
)
target_compile_features(definition PRIVATE cxx_std_20)
target_include_directories(definition PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_compile_options(definition PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_link_libraries(
    definition
    PRIVATE utils
    PUBLIC dictionary_data
    PUBLIC Qt6::Core
    PUBLIC Qt6::Gui
    PUBLIC Qt6::Quick
    PUBLIC settings
)
