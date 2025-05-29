include(FetchContent)

FetchContent_Declare(
  glaze
  GIT_REPOSITORY https://github.com/stephenberry/glaze.git
  GIT_TAG ff57af07fe47a1419e13c29dbb8d8cbacbd1c8a1 # v5.0.0
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(glaze)

register_dependency(Glaze DOWNLOADED 5.0.0)

add_library(dep_glaze INTERFACE)
target_link_libraries(dep_glaze INTERFACE glaze::glaze)
