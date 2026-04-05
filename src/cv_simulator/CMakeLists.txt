cmake_minimum_required(VERSION 3.11.0)

project(cv_simulator VERSION 0.1.0 LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

if(TARGET sensor_msgs::sensor_msgs__rosidl_typesupport_cpp)
  set(SENSORMSGS_TS sensor_msgs::sensor_msgs__rosidl_typesupport_cpp)
elseif(TARGET sensor_msgs::sensor_msgs__rosidl_typesupport_fastrtps_cpp)
  set(SENSORMSGS_TS sensor_msgs::sensor_msgs__rosidl_typesupport_fastrtps_cpp)
elseif(TARGET sensor_msgs::sensor_msgs__rosidl_typesupport_introspection_cpp)
  set(SENSORMSGS_TS sensor_msgs::sensor_msgs__rosidl_typesupport_introspection_cpp)
else()
  message(FATAL_ERROR "sensor_msgs typesupport target not found. Did you source ROS 2?")
endif()

if (DEFINED VULKAN_SDK_PATH)
  set(Vulkan_INCLUDE_DIRS "${VULKAN_SDK_PATH}/Include")
  set(Vulkan_LIBRARY_DIRS "${VULKAN_SDK_PATH}/lib")
  set(Vulkan_LIBRARIES vulkan)
else()
  find_package(Vulkan REQUIRED)
  message(STATUS "Found Vulkan: $ENV{VULKAN_SDK}")
  if (TARGET Vulkan::Vulkan)
        set(Vulkan_INCLUDE_DIRS "") 
        set(Vulkan_LIBRARY_DIRS "")
        set(Vulkan_LIBRARIES Vulkan::Vulkan)
    endif()
endif()

if (DEFINED GLFW_PATH)
    message(STATUS "Using manual GLFW path: ${GLFW_PATH}")
    set(GLFW_INCLUDE_DIRS "${GLFW_PATH}/include")
    set(GLFW_LIBRARY_DIRS "${GLFW_PATH}/lib")
    set(GLFW_LIBRARIES glfw)
else()
    find_package(glfw3 3.3 REQUIRED)
    message(STATUS "Found GLFW via find_package")
    set(GLFW_INCLUDE_DIRS "")
    set(GLFW_LIBRARY_DIRS "")
    set(GLFW_LIBRARIES glfw)
endif()

if (DEFINED GLM_PATH)
  set(GLM_INCLUDE_DIRS ${GLM_PATH})
else()
  find_package(glm QUIET)
  if (TARGET glm::glm)
    set(GLM_INCLUDE_DIRS "")
    set(USE_GLM_TARGET TRUE)
  else()
    set(GLM_INCLUDE_DIRS "")
  endif()
endif()

if (NOT DEFINED TINYOBJ_PATH)
    set(TINYOBJ_PATH external/tinyobjloader)
endif()

file(GLOB_RECURSE PROJECT_SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp")
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC
  ${PROJECT_SOURCE_DIR}/src/include
  ${PROJECT_SOURCE_DIR}/src/render/include
  ${PROJECT_SOURCE_DIR}/src/render/systems/include
  ${PROJECT_SOURCE_DIR}/external
  ${TINYOBJ_PATH}
  ${Vulkan_INCLUDE_DIRS}
  ${GLFW_INCLUDE_DIRS}
  ${GLM_INCLUDE_DIRS}
)

ament_target_dependencies(${PROJECT_NAME}
  rclcpp
  sensor_msgs
  geometry_msgs
)

target_link_libraries(${PROJECT_NAME}
  ${Vulkan_LIBRARIES}
  ${GLFW_LIBRARIES}
  ${SENSORMSGS_TS}
  nlohmann_json::nlohmann_json
  glm::glm
)

if (Vulkan_LIBRARY_DIRS)
  target_link_directories(${PROJECT_NAME} PUBLIC ${Vulkan_LIBRARY_DIRS})
endif()
if (GLFW_LIBRARY_DIRS)
  target_link_directories(${PROJECT_NAME} PUBLIC ${GLFW_LIBRARY_DIRS})
endif()

install(TARGETS ${PROJECT_NAME}
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY assets/
  DESTINATION share/${PROJECT_NAME}/assets
  FILES_MATCHING
  PATTERN "*.json"
  PATTERN "*.jpg"
  PATTERN "*.png"
  PATTERN "*.obj"
)

install(DIRECTORY shaders
  DESTINATION share/${PROJECT_NAME}
)

find_program(GLSL_VALIDATOR NAMES glslangValidator HINTS $ENV{VULKAN_SDK}/bin ${VULKAN_SDK_PATH}/bin /usr/bin /usr/local/bin)

if (NOT GLSL_VALIDATOR)
  message(WARNING "glslangValidator not found. Shader compilation will be skipped.")
else()
  file(GLOB_RECURSE GLSL_SOURCE_FILES "${PROJECT_SOURCE_DIR}/shaders/*.frag" "${PROJECT_SOURCE_DIR}/shaders/*.vert" "${PROJECT_SOURCE_DIR}/shaders/*.comp")
  set(SPIRV_BINARY_FILES)

  foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    get_filename_component(FILE_EXT ${GLSL} EXT) 
    set(SPIRV_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${FILE_NAME}.spv")

    add_custom_command(
      OUTPUT ${SPIRV_OUTPUT}
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
      COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV_OUTPUT}  
      DEPENDS ${GLSL}
      COMMENT "Compiling shader ${FILE_NAME}"
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV_OUTPUT})
  endforeach()

  add_custom_target(Shaders DEPENDS ${SPIRV_BINARY_FILES} COMMENT "Building Shaders")
  add_dependencies(${PROJECT_NAME} Shaders)

  install(DIRECTORY ${CMAKE_BINARY_DIR}/shaders/ DESTINATION share/${PROJECT_NAME}/shaders)
endif()

ament_export_dependencies(rclcpp sensor_msgs geometry_msgs nlohmann_json)
ament_package()