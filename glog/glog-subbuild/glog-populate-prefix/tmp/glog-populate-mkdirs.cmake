# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/fedor/CLionProjects/cpp-advanced-hse/glog/glog-src")
  file(MAKE_DIRECTORY "/home/fedor/CLionProjects/cpp-advanced-hse/glog/glog-src")
endif()
file(MAKE_DIRECTORY
  "/home/fedor/CLionProjects/cpp-advanced-hse/glog/glog-build"
  "/home/fedor/CLionProjects/cpp-advanced-hse/glog/glog-subbuild/glog-populate-prefix"
  "/home/fedor/CLionProjects/cpp-advanced-hse/glog/glog-subbuild/glog-populate-prefix/tmp"
  "/home/fedor/CLionProjects/cpp-advanced-hse/glog/glog-subbuild/glog-populate-prefix/src/glog-populate-stamp"
  "/home/fedor/CLionProjects/cpp-advanced-hse/glog/glog-subbuild/glog-populate-prefix/src"
  "/home/fedor/CLionProjects/cpp-advanced-hse/glog/glog-subbuild/glog-populate-prefix/src/glog-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/fedor/CLionProjects/cpp-advanced-hse/glog/glog-subbuild/glog-populate-prefix/src/glog-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/fedor/CLionProjects/cpp-advanced-hse/glog/glog-subbuild/glog-populate-prefix/src/glog-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
