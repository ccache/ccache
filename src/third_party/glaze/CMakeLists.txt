include(FetchContent)

FetchContent_Declare(
  glaze
  GIT_REPOSITORY https://github.com/stephenberry/glaze.git
  GIT_TAG v3.6.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glaze)

register_dependency(Glaze DOWNLOADED 3.6.2)

add_library(dep_glaze INTERFACE)
target_link_libraries(dep_glaze INTERFACE glaze::glaze)
