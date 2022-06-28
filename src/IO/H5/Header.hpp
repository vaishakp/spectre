// Distributed under the MIT License.
// See LICENSE.txt for details.

/// \file
/// Defines class h5::Header

#pragma once

#include <hdf5.h>
#include <string>

#include "IO/H5/Object.hpp"
#include "IO/H5/OpenGroup.hpp"

namespace h5 {
/*!
 * \ingroup HDF5Group
 * \brief Writes header info about the build, git commit, branch, etc.
 *
 * A Header object is used to store the ::info_from_build() result in the HDF5
 * files. The Header is automatically added to every single file by the
 * constructor of H5File.
 *
 * \example
 * You can read the header info out of an H5 file as shown in the example:
 * \snippet Test_H5File.cpp h5file_readwrite_get_header
 */
class Header : public h5::Object {
 public:
  /// \cond HIDDEN_SYMOLS
  static std::string extension() { return ".hdr"; }

  Header(bool exists, detail::OpenGroup&& group, hid_t location,
         const std::string& name);

  Header(const Header& /*rhs*/) = delete;
  Header& operator=(const Header& /*rhs*/) = delete;

  Header(Header&& /*rhs*/) = delete;             // NOLINT
  Header& operator=(Header&& /*rhs*/) = delete;  // NOLINT

  ~Header() override = default;
  /// \endcond

  const std::string& get_header() const { return header_info_; }

  /// Returns the environment variables at compile time of the simulation that
  /// produced the file
  std::string get_env_variables() const;

  /// Returns the contents of the `BuildInfo.txt` file generated by CMake
  /// of the simulation that produced the file.
  std::string get_build_info() const;

  const std::string& subfile_path() const override { return path_; }

 private:
  /// \cond HIDDEN_SYMBOLS
  detail::OpenGroup group_;
  std::string environment_variables_;
  std::string build_info_;
  std::string header_info_;
  std::string path_;

  static const std::string printenv_delimiter_;
  static const std::string build_info_delimiter_;
  /// \endcond
};
}  // namespace h5
