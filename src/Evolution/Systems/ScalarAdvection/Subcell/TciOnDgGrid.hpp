// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstddef>
#include <tuple>

#include "DataStructures/Tensor/TypeAliases.hpp"
#include "Domain/Tags.hpp"
#include "Evolution/DgSubcell/RdmpTciData.hpp"
#include "Evolution/DgSubcell/Tags/DataForRdmpTci.hpp"
#include "Evolution/DgSubcell/Tags/Mesh.hpp"
#include "Evolution/DgSubcell/Tags/SubcellOptions.hpp"
#include "Evolution/Systems/ScalarAdvection/Tags.hpp"
#include "Utilities/TMPL.hpp"

/// \cond
class DataVector;
template <size_t Dim>
class Mesh;
/// \endcond

namespace ScalarAdvection::subcell {
/*!
 * \brief The troubled-cell indicator run on the DG grid to check if the
 * solution is admissible.
 *
 * Applies the Persson and RDMP TCI to \f$U\f$.
 */
template <size_t Dim>
struct TciOnDgGrid {
 public:
  using return_tags = tmpl::list<>;
  using argument_tags =
      tmpl::list<ScalarAdvection::Tags::U, ::domain::Tags::Mesh<Dim>,
                 evolution::dg::subcell::Tags::Mesh<Dim>,
                 evolution::dg::subcell::Tags::DataForRdmpTci,
                 evolution::dg::subcell::Tags::SubcellOptions>;

  static std::tuple<bool, evolution::dg::subcell::RdmpTciData> apply(
      const Scalar<DataVector>& dg_u, const Mesh<Dim>& dg_mesh,
      const Mesh<Dim>& subcell_mesh,
      const evolution::dg::subcell::RdmpTciData& past_rdmp_tci_data,
      const evolution::dg::subcell::SubcellOptions& subcell_options,
      double persson_exponent);
};
}  // namespace ScalarAdvection::subcell
