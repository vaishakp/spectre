// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>

#include "DataStructures/DataBox/DataBox.hpp"
#include "DataStructures/DataBox/Prefixes.hpp"
#include "DataStructures/DataVector.hpp"
#include "DataStructures/ModalVector.hpp"
#include "DataStructures/Variables.hpp"
#include "Domain/Structure/ElementId.hpp"
#include "Domain/Tags.hpp"
#include "Evolution/Systems/CurvedScalarWave/Worldtube/Inboxes.hpp"
#include "Evolution/Systems/CurvedScalarWave/Worldtube/SingletonActions/UpdateAcceleration.hpp"
#include "Evolution/Systems/CurvedScalarWave/Worldtube/Tags.hpp"
#include "NumericalAlgorithms/SphericalHarmonics/Tags.hpp"
#include "NumericalAlgorithms/SphericalHarmonics/YlmToStf.hpp"
#include "Parallel/AlgorithmExecution.hpp"
#include "Parallel/GlobalCache.hpp"
#include "ParallelAlgorithms/Actions/MutateApply.hpp"
#include "ParallelAlgorithms/Initialization/MutateAssign.hpp"
#include "Time/TimeStepId.hpp"
#include "Utilities/ErrorHandling/Assert.hpp"
#include "Utilities/Gsl.hpp"
#include "Utilities/TMPL.hpp"

/// \cond
namespace Tags {
struct TimeStepId;
}  // namespace Tags
/// \endcond

namespace CurvedScalarWave::Worldtube::Actions {
/*!
 * \brief Adds up the spherical harmonic projections from the different elements
 * abutting the worldtube.
 *
 * \details This action currently assumes that there is no h-refinement
 * ocurring in the elements abutting the worldtubes. This could be accounted for
 * by checking that data from at least one element has been sent from each
 * abutting block and then using its `ElementId` to figure out the current
 * refinement level and therefore how many elements are expected to send data
 * for each block.
 *
 * DataBox:
 * - Uses:
 *    - `Worldtube::Tags::ExpansionOrder`
 *    - `Worldtube::Tags::ExcisionSphere`
 *    - `Worldtube::Tags::ElementFacesGridCoordinates`
 *    - `Tags::TimeStepId`
 * - Mutates:
 *    - `Stf::Tags::StfTensor<Tags::PsiWorldtube, 0, Dim, Frame::Inertial>`
 *    - `Stf::Tags::StfTensor<::Tags::dt<Tags::PsiWorldtube>, 0, Dim,
 *                                     Frame::Inertial>`
 *    - `Stf::Tags::StfTensor<Tags::PsiWorldtube, 1, Dim, Frame::Inertial>`
 *    - `Stf::Tags::StfTensor<::Tags::dt<Tags::PsiWorldtube>, 1, Dim,
 *                                     Frame::Inertial>`
 */
struct ReceiveElementData {
  static constexpr size_t Dim = 3;
  using tags_list = tmpl::list<CurvedScalarWave::Tags::Psi,
                               ::Tags::dt<CurvedScalarWave::Tags::Psi>>;
  using inbox_tags = tmpl::list<
      ::CurvedScalarWave::Worldtube::Tags::SphericalHarmonicsInbox<Dim>>;
  using simple_tags = tmpl::list<
      Stf::Tags::StfTensor<Tags::PsiWorldtube, 0, Dim, Frame::Inertial>,
      Stf::Tags::StfTensor<::Tags::dt<Tags::PsiWorldtube>, 0, Dim,
                           Frame::Inertial>,
      Stf::Tags::StfTensor<Tags::PsiWorldtube, 1, Dim, Frame::Inertial>,
      Stf::Tags::StfTensor<::Tags::dt<Tags::PsiWorldtube>, 1, Dim,
                           Frame::Inertial>>;

  template <typename DbTagsList, typename... InboxTags, typename Metavariables,
            typename ArrayIndex, typename ActionList,
            typename ParallelComponent>
  static Parallel::iterable_action_return_t apply(
      db::DataBox<DbTagsList>& box, tuples::TaggedTuple<InboxTags...>& inboxes,
      const Parallel::GlobalCache<Metavariables>& /*cache*/,
      const ArrayIndex& /*array_index*/, ActionList /*meta*/,
      const ParallelComponent* const /*meta*/) {
    const size_t expected_number_of_senders =
        db::get<Tags::ElementFacesGridCoordinates<Dim>>(box).size();
    const auto& time_step_id = db::get<::Tags::TimeStepId>(box);
    auto& inbox = tuples::get<Tags::SphericalHarmonicsInbox<Dim>>(inboxes);
    if (inbox.count(time_step_id) == 0 or
        inbox.at(time_step_id).size() < expected_number_of_senders) {
      return {Parallel::AlgorithmExecution::Retry, std::nullopt};
    }
    ASSERT(inbox.at(time_step_id).size() == expected_number_of_senders,
           "Expected data from "
               << expected_number_of_senders << " senders, but received "
               << inbox.at(time_step_id).size() << " for TimeStepId "
               << time_step_id);
    const size_t order = db::get<Tags::ExpansionOrder>(box);
    const size_t num_modes = (order + 1) * (order + 1);

    Variables<tags_list> external_ylm_coefs{num_modes, 0.};
    for (const auto& [_, element_ylm_coefs] : inbox.at(time_step_id)) {
      external_ylm_coefs += element_ylm_coefs;
    }
    const double wt_radius = db::get<Tags::ExcisionSphere<Dim>>(box).radius();
    external_ylm_coefs /= wt_radius * wt_radius;

    DataVector& psi_ylm_coefs =
        get(get<CurvedScalarWave::Tags::Psi>(external_ylm_coefs));
    DataVector& dt_psi_ylm_coefs =
        get(get<::Tags::dt<CurvedScalarWave::Tags::Psi>>(external_ylm_coefs));

    const ModalVector psi_ylm_l0(&psi_ylm_coefs[0], 1);
    const ModalVector dt_psi_ylm_l0(&dt_psi_ylm_coefs[0], 1);
    tnsr::i<double, Dim, Frame::Inertial> psi_stf_l1{};
    tnsr::i<double, Dim, Frame::Inertial> dt_psi_stf_l1{};
    if (order > 0) {
      ModalVector psi_ylm_l1(&psi_ylm_coefs[1], 3);
      ModalVector dt_psi_ylm_l1(&dt_psi_ylm_coefs[1], 3);
      psi_ylm_l1 /= wt_radius;
      dt_psi_ylm_l1 /= wt_radius;
      psi_stf_l1 = ylm::ylm_to_stf_1<Frame::Inertial>(psi_ylm_l1);
      dt_psi_stf_l1 = ylm::ylm_to_stf_1<Frame::Inertial>(dt_psi_ylm_l1);
    }

    ::Initialization::mutate_assign<simple_tags>(
        make_not_null(&box), ylm::ylm_to_stf_0(psi_ylm_l0),
        ylm::ylm_to_stf_0(dt_psi_ylm_l0), std::move(psi_stf_l1),
        std::move(dt_psi_stf_l1));
    inbox.erase(time_step_id);
    if (db::get<Tags::CurrentIteration>(box) + 1 <
        db::get<Tags::MaxIterations>(box)) {
      db::mutate<Tags::CurrentIteration>(
          [](const gsl::not_null<size_t*> current_iteration) {
            *current_iteration += 1;
          },
          make_not_null(&box));
      // still iterating, go to `IterateAccelerationTerms`
      return {Parallel::AlgorithmExecution::Continue, std::nullopt};

    } else {
      db::mutate<Tags::CurrentIteration>(
          [](const gsl::not_null<size_t*> current_iteration) {
            *current_iteration = 0;
          },
          make_not_null(&box));
      // done iterating
      return {
          Parallel::AlgorithmExecution::Continue,
          tmpl::index_of<ActionList,
                         ::Actions::MutateApply<UpdateAcceleration>>::value};
    }
  }
};
}  // namespace CurvedScalarWave::Worldtube::Actions
