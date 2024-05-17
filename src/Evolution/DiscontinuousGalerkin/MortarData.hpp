// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <pup.h>
#include <string>
#include <utility>

#include "DataStructures/DataVector.hpp"
#include "DataStructures/Tensor/TypeAliases.hpp"
#include "NumericalAlgorithms/Spectral/Mesh.hpp"
#include "Utilities/Gsl.hpp"
#include "Utilities/Serialization/PupStlCpp17.hpp"

namespace evolution::dg {
/*!
 * \brief Data on the mortar used to compute the boundary correction for the
 * DG scheme.
 *
 * The class holds the local data that has been projected to the mortar as well
 * as the neighbor data that has been projected to the mortar. The local and
 * neighbor data is later used to compute the same unique boundary correction on
 * the mortar for both elements. That is, the final boundary correction
 * computation is done twice: once on each element touching the mortar. However,
 * the computation is done in such a way that the results agree.
 *
 * In addition to the (type-erased) fields on both sides of the mortar, the face
 * (not mortar!) mesh of the neighbor is stored. The mesh will be necessary
 * when hybridizing DG with finite difference or finite volume schemes
 * (DG-subcell).
 *
 * If the element and its neighbor have unaligned logical coordinate systems
 * then the data is stored in the local logical coordinate's orientation
 * (\f$\xi\f$ varies fastest). This means the action sending the data is
 * responsible for reorienting the data on the mortar so it matches the
 * neighbor's orientation.
 *
 * \tparam Dim the volume dimension of the mesh
 */
template <size_t Dim>
class MortarData {
  using MortarType = std::optional<std::pair<Mesh<Dim - 1>, DataVector>>;

 public:
  /*!
   * \brief Insert the magnitude of the local face normal, the determinant
   * of the volume inverse Jacobian, and the determinant of the face Jacobian.
   * Used for local time stepping with Gauss points.
   *
   * The magnitude of the face normal is given by:
   *
   * \f{align*}{
   *  \sqrt{
   *   \frac{\partial\xi}{\partial x^i} \gamma^{ij}
   *   \frac{\partial\xi}{\partial x^j}}
   * \f}
   *
   * for a face in the \f$\xi\f$-direction, with inverse spatial metric
   * \f$\gamma^{ij}\f$.
   */
  void insert_local_geometric_quantities(
      const Scalar<DataVector>& local_volume_det_inv_jacobian,
      const Scalar<DataVector>& local_face_det_jacobian,
      const Scalar<DataVector>& local_face_normal_magnitude);

  /*!
   * \brief Insert the magnitude of the local face normal. Used for local time
   * stepping with Gauss-Lobatto points.
   *
   * The magnitude of the face normal is given by:
   *
   * \f{align*}{
   *  \sqrt{
   *   \frac{\partial\xi}{\partial x^i} \gamma^{ij}
   *   \frac{\partial\xi}{\partial x^j}}
   * \f}
   *
   * for a face in the \f$\xi\f$-direction, with inverse spatial metric
   * \f$\gamma^{ij}\f$.
   */
  void insert_local_face_normal_magnitude(
      const Scalar<DataVector>& local_face_normal_magnitude);

  /*!
   * \brief Sets the `local_volume_det_inv_jacobian` by setting the DataVector
   * to point into the `MortarData`'s internal storage.
   *
   * \warning The result should never be changed.
   */
  void get_local_volume_det_inv_jacobian(
      gsl::not_null<Scalar<DataVector>*> local_volume_det_inv_jacobian) const;

  /*!
   * \brief Sets the `local_face_det_jacobian` by setting the DataVector to
   * point into the `MortarData`'s internal storage.
   *
   * \warning The result should never be changed.
   */
  void get_local_face_det_jacobian(
      gsl::not_null<Scalar<DataVector>*> local_face_det_jacobian) const;

  /*!
   * \brief Sets the `local_face_normal_magnitude` by setting the DataVector to
   * point into the `MortarData`'s internal storage.
   *
   * \warning The result should never be changed.
   */
  void get_local_face_normal_magnitude(
      gsl::not_null<Scalar<DataVector>*> local_face_normal_magnitude) const;

  auto local_mortar_data() const
      -> const std::optional<std::pair<Mesh<Dim - 1>, DataVector>>& {
    return local_mortar_data_;
  }

  auto neighbor_mortar_data() const
      -> const std::optional<std::pair<Mesh<Dim - 1>, DataVector>>& {
    return neighbor_mortar_data_;
  }

  auto local_mortar_data()
      -> std::optional<std::pair<Mesh<Dim - 1>, DataVector>>& {
    return local_mortar_data_;
  }

  auto neighbor_mortar_data()
      -> std::optional<std::pair<Mesh<Dim - 1>, DataVector>>& {
    return neighbor_mortar_data_;
  }

  // NOLINTNEXTLINE(google-runtime-references)
  void pup(PUP::er& p);

 private:
  template <size_t LocalDim>
  // NOLINTNEXTLINE
  friend bool operator==(const MortarData<LocalDim>& lhs,
                         const MortarData<LocalDim>& rhs);

  MortarType local_mortar_data_{};
  MortarType neighbor_mortar_data_{};
  DataVector local_geometric_quantities_{};
  bool using_volume_and_face_jacobians_{false};
  bool using_only_face_normal_magnitude_{false};
};

template <size_t Dim>
bool operator!=(const MortarData<Dim>& lhs, const MortarData<Dim>& rhs);

template <size_t Dim>
std::ostream& operator<<(std::ostream& os, const MortarData<Dim>& mortar_data);
}  // namespace evolution::dg
