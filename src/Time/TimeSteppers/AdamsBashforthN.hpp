// Distributed under the MIT License.
// See LICENSE.txt for details.

/// \file
/// Defines class AdamsBashforthN

#pragma once

#include <algorithm>
#include <boost/iterator/transform_iterator.hpp>
#include <cstddef>
#include <iosfwd>
#include <iterator>
#include <limits>
#include <map>
#include <pup.h>
#include <tuple>
#include <vector>

#include "NumericalAlgorithms/Interpolation/LagrangePolynomial.hpp"
#include "Options/Options.hpp"
#include "Parallel/CharmPupable.hpp"
#include "Time/BoundaryHistory.hpp"
#include "Time/EvolutionOrdering.hpp"
#include "Time/Time.hpp"
#include "Time/TimeStepId.hpp"
#include "Time/TimeSteppers/TimeStepper.hpp"  // IWYU pragma: keep
#include "Utilities/CachedFunction.hpp"
#include "Utilities/ErrorHandling/Assert.hpp"
#include "Utilities/Gsl.hpp"
#include "Utilities/Overloader.hpp"
#include "Utilities/TMPL.hpp"

/// \cond
namespace TimeSteppers {
template <typename T>
class UntypedHistory;
}  // namespace TimeSteppers
/// \endcond

namespace TimeSteppers {

/*!
 * \ingroup TimeSteppersGroup
 *
 * An Nth order Adams-Bashforth time stepper.
 *
 * The stable step size factors for different orders are given by:
 *
 * <table class="doxtable">
 *  <tr>
 *    <th> %Order </th>
 *    <th> CFL Factor </th>
 *  </tr>
 *  <tr>
 *    <td> 1 </td>
 *    <td> 1 </td>
 *  </tr>
 *  <tr>
 *    <td> 2 </td>
 *    <td> 1 / 2 </td>
 *  </tr>
 *  <tr>
 *    <td> 3 </td>
 *    <td> 3 / 11 </td>
 *  </tr>
 *  <tr>
 *    <td> 4 </td>
 *    <td> 3 / 20 </td>
 *  </tr>
 *  <tr>
 *    <td> 5 </td>
 *    <td> 45 / 551 </td>
 *  </tr>
 *  <tr>
 *    <td> 6 </td>
 *    <td> 5 / 114 </td>
 *  </tr>
 *  <tr>
 *    <td> 7 </td>
 *    <td> 945 / 40663 </td>
 *  </tr>
 *  <tr>
 *    <td> 8 </td>
 *    <td> 945 / 77432 </td>
 *  </tr>
 * </table>
 */
class AdamsBashforthN : public LtsTimeStepper::Inherit {
 public:
  static constexpr const size_t maximum_order = 8;

  struct Order {
    using type = size_t;
    static constexpr Options::String help = {"Convergence order"};
    static type lower_bound() { return 1; }
    static type upper_bound() { return maximum_order; }
  };
  using options = tmpl::list<Order>;
  static constexpr Options::String help = {
      "An Adams-Bashforth Nth order time-stepper."};

  AdamsBashforthN() = default;
  explicit AdamsBashforthN(size_t order);
  AdamsBashforthN(const AdamsBashforthN&) = default;
  AdamsBashforthN& operator=(const AdamsBashforthN&) = default;
  AdamsBashforthN(AdamsBashforthN&&) = default;
  AdamsBashforthN& operator=(AdamsBashforthN&&) = default;
  ~AdamsBashforthN() override = default;

  /*!
   * An explanation of the computation being performed by this
   * function:
   * \f$\newcommand\tL{t^L}\newcommand\tR{t^R}\newcommand\tU{\tilde{t}\!}
   * \newcommand\mat{\mathbf}\f$
   *
   * Suppose the local and remote sides of the interface are evaluated
   * at times \f$\ldots, \tL_{-1}, \tL_0, \tL_1, \ldots\f$ and
   * \f$\ldots, \tR_{-1}, \tR_0, \tR_1, \ldots\f$, respectively, with
   * the starting location of the numbering arbitrary in each case.
   * Let the step we wish to calculate the effect of be the step from
   * \f$\tL_{m_S}\f$ to \f$\tL_{m_S+1}\f$.  We call the sequence
   * produced from the union of the local and remote time sequences
   * \f$\ldots, \tU_{-1}, \tU_0, \tU_1, \ldots\f$.  For example, one
   * possible sequence of times is:
   * \f{equation}
   *   \begin{aligned}
   *     \text{Local side:} \\ \text{Union times:} \\ \text{Remote side:}
   *   \end{aligned}
   *   \cdots
   *   \begin{gathered}
   *     \, \\ \tU_1 \\ \tR_5
   *   \end{gathered}
   *   \leftarrow \Delta \tU_1 \rightarrow
   *   \begin{gathered}
   *     \tL_4 \\ \tU_2 \\ \,
   *   \end{gathered}
   *   \leftarrow \Delta \tU_2 \rightarrow
   *   \begin{gathered}
   *     \, \\ \tU_3 \\ \tR_6
   *   \end{gathered}
   *   \leftarrow \Delta \tU_3 \rightarrow
   *   \begin{gathered}
   *    \, \\ \tU_4 \\ \tR_7
   *   \end{gathered}
   *   \leftarrow \Delta \tU_4 \rightarrow
   *   \begin{gathered}
   *     \tL_5 \\ \tU_5 \\ \,
   *   \end{gathered}
   *   \cdots
   * \f}
   * We call the indices of the step's start and end times in the
   * union time sequence \f$n_S\f$ and \f$n_E\f$, respectively.  We
   * define \f$n^L_m\f$ to be the union-time index corresponding to
   * \f$\tL_m\f$ and \f$m^L_n\f$ to be the index of the last local
   * time not later than \f$\tU_n\f$ and similarly for the remote
   * side.  So for the above example, \f$n^L_4 = 2\f$ and \f$m^R_2 =
   * 5\f$, and if we wish to compute the step from \f$\tL_4\f$ to
   * \f$\tL_5\f$ we would have \f$m_S = 4\f$, \f$n_S = 2\f$, and
   * \f$n_E = 5\f$.
   *
   * If we wish to evaluate the change over this step to \f$k\f$th
   * order, we can write the change in the value as a linear
   * combination of the values of the coupling between the elements at
   * unequal times:
   * \f{equation}
   *   \mat{F}_{m_S} =
   *   \mspace{-10mu}
   *   \sum_{q^L = m_S-(k-1)}^{m_S}
   *   \,
   *   \sum_{q^R = m^R_{n_S}-(k-1)}^{m^R_{n_E-1}}
   *   \mspace{-10mu}
   *   \mat{D}_{q^Lq^R}
   *   I_{q^Lq^R},
   * \f}
   * where \f$\mat{D}_{q^Lq^R}\f$ is the coupling function evaluated
   * between data from \f$\tL_{q^L}\f$ and \f$\tR_{q^R}\f$.  The
   * coefficients can be written as the sum of three terms,
   * \f{equation}
   *   I_{q^Lq^R} = I^E_{q^Lq^R} + I^R_{q^Lq^R} + I^L_{q^Lq^R},
   * \f}
   * which can be interpreted as a contribution from equal-time
   * evaluations and contributions related to the remote and local
   * evaluation times.  These are given by
   * \f{align}
   *   I^E_{q^Lq^R} &=
   *   \mspace{-10mu}
   *   \sum_{n=n_S}^{\min\left\{n_E, n^L+k\right\}-1}
   *   \mspace{-10mu}
   *   \tilde{\alpha}_{n,n-n^L} \Delta \tU_n
   *   &&\text{if $\tL_{q^L} = \tR_{q^R}$, otherwise 0}
   *   \\
   *   I^R_{q^Lq^R} &=
   *   \ell_{q^L - m_S + k}\!\left(
   *     \tU_{n^R}; \tL_{m_S - (k-1)}, \ldots, \tL_{m_S}\right)
   *   \mspace{-10mu}
   *   \sum_{n=\max\left\{n_S, n^R\right\}}
   *       ^{\min\left\{n_E, n^R+k\right\}-1}
   *   \mspace{-10mu}
   *   \tilde{\alpha}_{n,n-n^R} \Delta \tU_n
   *   &&\text{if $\tR_{q^R}$ is not in $\{\tL_{\vphantom{|}\cdots}\}$,
   *     otherwise 0}
   *   \\
   *   I^L_{q^Lq^R} &=
   *   \mspace{-10mu}
   *   \sum_{n=\max\left\{n_S, n^R\right\}}
   *       ^{\min\left\{n_E, n^L+k, n^R_{q^R+k}\right\}-1}
   *   \mspace{-10mu}
   *   \ell_{q^R - m^R_n + k}\!\left(\tU_{n^L};
   *     \tR_{m^R_n - (k-1)}, \ldots, \tR_{m^R_n}\right)
   *   \tilde{\alpha}_{n,n-n^L} \Delta \tU_n
   *   &&\text{if $\tL_{q^L}$ is not in $\{\tR_{\vphantom{|}\cdots}\}$,
   *     otherwise 0,}
   * \f}
   * where for brevity we write \f$n^L = n^L_{q^L}\f$ and \f$n^R =
   * n^R_{q^R}\f$, and where \f$\ell_a(t; x_1, \ldots, x_k)\f$ a
   * Lagrange interpolating polynomial and \f$\tilde{\alpha}_{nj}\f$
   * is the \f$j\f$th coefficient for an Adams-Bashforth step over the
   * union times from step \f$n\f$ to step \f$n+1\f$.
   */
  template <typename LocalVars, typename RemoteVars, typename Coupling>
  void add_boundary_delta(
      gsl::not_null<BoundaryReturn<LocalVars, RemoteVars, Coupling>*> result,
      gsl::not_null<BoundaryHistoryType<LocalVars, RemoteVars, Coupling>*>
          history,
      const TimeDelta& time_step, const Coupling& coupling) const;

  template <typename LocalVars, typename RemoteVars, typename Coupling>
  void boundary_dense_output(
      gsl::not_null<BoundaryReturn<LocalVars, RemoteVars, Coupling>*> result,
      const BoundaryHistoryType<LocalVars, RemoteVars, Coupling>& history,
      double time, const Coupling& coupling) const;

  size_t order() const override;

  size_t error_estimate_order() const override;

  size_t number_of_past_steps() const override;

  double stable_step() const override;

  TimeStepId next_time_id(const TimeStepId& current_id,
                          const TimeDelta& time_step) const override;

  WRAPPED_PUPable_decl_template(AdamsBashforthN);  // NOLINT

  explicit AdamsBashforthN(CkMigrateMessage* /*unused*/) {}

  // clang-tidy: do not pass by non-const reference
  void pup(PUP::er& p) override;  // NOLINT

 private:
  friend bool operator==(const AdamsBashforthN& lhs,
                         const AdamsBashforthN& rhs);

  // Some of the private methods take a parameter of type "Delta" or
  // "TimeType".  Delta is expected to be a TimeDelta or an
  // ApproximateTimeDelta, and TimeType is expected to be a Time or an
  // ApproximateTime.  The former cases will detect and optimize the
  // constant-time-step case, while the latter are necessary for dense
  // output.
  template <typename T>
  void update_u_impl(gsl::not_null<T*> u,
                     gsl::not_null<UntypedHistory<T>*> history,
                     const TimeDelta& time_step) const;

  template <typename T>
  bool update_u_impl(gsl::not_null<T*> u, gsl::not_null<T*> u_error,
                     gsl::not_null<UntypedHistory<T>*> history,
                     const TimeDelta& time_step) const;

  template <typename T>
  bool dense_update_u_impl(gsl::not_null<T*> u,
                           const UntypedHistory<T>& history, double time) const;

  template <typename T, typename Delta>
  void update_u_common(gsl::not_null<T*> u, const UntypedHistory<T>& history,
                       const Delta& time_step, size_t order) const;

  template <typename T>
  bool can_change_step_size_impl(const TimeStepId& time_id,
                                 const UntypedHistory<T>& history) const;

  template <typename T, typename TimeType>
  void boundary_impl(gsl::not_null<T*> result,
                     const BoundaryHistoryEvaluator<T>& coupling,
                     const TimeType& end_time) const;

  /// Get coefficients for a time step.  Arguments are an iterator
  /// pair to past times, oldest to newest, and the time step to take.
  template <typename Iterator, typename Delta>
  static std::vector<double> get_coefficients(const Iterator& times_begin,
                                              const Iterator& times_end,
                                              const Delta& step);

  static std::vector<double> get_coefficients_impl(
      const std::vector<double>& steps);

  static std::vector<double> variable_coefficients(
      const std::vector<double>& steps);

  static std::vector<double> constant_coefficients(size_t order);

  struct ApproximateTimeDelta;

  // Time-like interface to a double used for dense output
  struct ApproximateTime {
    double time = std::numeric_limits<double>::signaling_NaN();
    double value() const { return time; }

    // Only the operators that are actually used are defined.
    friend ApproximateTimeDelta operator-(const ApproximateTime& a,
                                          const Time& b) {
      return {a.value() - b.value()};
    }

    friend bool operator<(const Time& a, const ApproximateTime& b) {
      return a.value() < b.value();
    }

    friend bool operator<(const ApproximateTime& a, const Time& b) {
      return a.value() < b.value();
    }

    friend std::ostream& operator<<(std::ostream& s, const ApproximateTime& t) {
      return s << t.value();
    }
  };

  // TimeDelta-like interface to a double used for dense output
  struct ApproximateTimeDelta {
    double delta = std::numeric_limits<double>::signaling_NaN();
    double value() const { return delta; }
    bool is_positive() const { return delta > 0.; }

    // Only the operators that are actually used are defined.
    friend bool operator<(const ApproximateTimeDelta& a,
                          const ApproximateTimeDelta& b) {
      return a.value() < b.value();
    }
  };

  TIME_STEPPER_DECLARE_OVERLOADS

  size_t order_ = 3;
};

bool operator!=(const AdamsBashforthN& lhs, const AdamsBashforthN& rhs);

template <typename LocalVars, typename RemoteVars, typename Coupling>
void AdamsBashforthN::add_boundary_delta(
    const gsl::not_null<BoundaryReturn<LocalVars, RemoteVars, Coupling>*>
        result,
    const gsl::not_null<BoundaryHistoryType<LocalVars, RemoteVars, Coupling>*>
        history,
    const TimeDelta& time_step, const Coupling& coupling) const {
  const auto cleaner = history->cleaner();
  const auto signed_order =
      static_cast<typename decltype(cleaner.local_end())::difference_type>(
          cleaner.integration_order());

  ASSERT(cleaner.local_size() >= cleaner.integration_order(),
         "Insufficient data to take an order-" << cleaner.integration_order()
         << " step.  Have " << cleaner.local_size() << " times, need "
         << cleaner.integration_order());
  cleaner.local_mark_unneeded(cleaner.local_end() - signed_order);

  if (std::equal(cleaner.local_begin(), cleaner.local_end(),
                 cleaner.remote_end() - signed_order)) {
    // GTS
    ASSERT(cleaner.remote_size() >= cleaner.integration_order(),
           "Insufficient data to take an order-" << cleaner.integration_order()
           << " step.  Have " << cleaner.remote_size() << " times, need "
           << cleaner.integration_order());
    cleaner.remote_mark_unneeded(cleaner.remote_end() - signed_order);
  } else {
    const auto remote_step_for_step_start =
        std::upper_bound(cleaner.remote_begin(), cleaner.remote_end(),
                         *(cleaner.local_end() - 1),
                         evolution_less<Time>{time_step.is_positive()});
    ASSERT(remote_step_for_step_start - cleaner.remote_begin() >= signed_order,
           "Insufficient data to take an order-" << cleaner.integration_order()
           << " step.  Have "
           << remote_step_for_step_start - cleaner.remote_begin()
           << " times before the step, need " << cleaner.integration_order());
    cleaner.remote_mark_unneeded(remote_step_for_step_start - signed_order);
  }

  boundary_impl(&*make_math_wrapper(result), history->evaluator(coupling),
                *(cleaner.local_end() - 1) + time_step);
}

template <typename LocalVars, typename RemoteVars, typename Coupling>
void AdamsBashforthN::boundary_dense_output(
    const gsl::not_null<BoundaryReturn<LocalVars, RemoteVars, Coupling>*>
        result,
    const BoundaryHistoryType<LocalVars, RemoteVars, Coupling>& history,
    const double time, const Coupling& coupling) const {
  return boundary_impl(&*make_math_wrapper(result), history.evaluator(coupling),
                       ApproximateTime{time});
}

template <typename T, typename TimeType>
void AdamsBashforthN::boundary_impl(const gsl::not_null<T*> result,
                                    const BoundaryHistoryEvaluator<T>& coupling,
                                    const TimeType& end_time) const {
  // Might be different from order_ during self-start.
  const auto current_order = coupling.integration_order();

  ASSERT(current_order <= order_,
         "Local history is too long for target order (" << current_order
         << " should not exceed " << order_ << ")");
  ASSERT(coupling.remote_size() >= current_order,
         "Remote history is too short (" << coupling.remote_size()
         << " should be at least " << current_order << ")");

  // Avoid billions of casts
  const auto order_s = static_cast<
      typename BoundaryHistoryEvaluator<T>::iterator::difference_type>(
      current_order);

  // Start and end of the step we are trying to take
  const Time start_time = *(coupling.local_end() - 1);
  const auto time_step = end_time - start_time;

  // We define the local_begin and remote_begin variables as the start
  // of the part of the history relevant to this calculation.
  // Boundary history cleanup happens immediately before the step, but
  // boundary dense output happens before that, so there may be data
  // left over that was needed for the previous step and has not been
  // cleaned out yet.
  const auto local_begin = coupling.local_end() - order_s;

  if (std::equal(local_begin, coupling.local_end(),
                 coupling.remote_end() - order_s)) {
    // No local time-stepping going on.
    const auto coefficients =
        get_coefficients(local_begin, coupling.local_end(), time_step);

    auto local_it = local_begin;
    auto remote_it = coupling.remote_end() - order_s;
    for (auto coefficients_it = coefficients.rbegin();
         coefficients_it != coefficients.rend();
         ++coefficients_it, ++local_it, ++remote_it) {
      *result +=
          time_step.value() * *coefficients_it * *coupling(local_it, remote_it);
    }
    return;
  }

  ASSERT(current_order == order_,
         "Cannot perform local time-stepping while self-starting.");

  const evolution_less<> less{time_step.is_positive()};
  const auto remote_begin =
      std::upper_bound(coupling.remote_begin(), coupling.remote_end(),
                       start_time, less) -
      order_s;

  ASSERT(std::is_sorted(local_begin, coupling.local_end(), less),
         "Local history not in order");
  ASSERT(std::is_sorted(remote_begin, coupling.remote_end(), less),
         "Remote history not in order");
  ASSERT(not less(start_time, *(remote_begin + (order_s - 1))),
         "Remote history does not extend far enough back");
  ASSERT(less(*(coupling.remote_end() - 1), end_time),
         "Please supply only older data: " << *(coupling.remote_end() - 1)
         << " is not before " << end_time);

  // Union of times of all step boundaries on any side.
  const auto union_times = [&coupling, &local_begin, &remote_begin, &less]() {
    std::vector<Time> ret;
    ret.reserve(coupling.local_size() + coupling.remote_size());
    std::set_union(local_begin, coupling.local_end(), remote_begin,
                   coupling.remote_end(), std::back_inserter(ret), less);
    return ret;
  }();

  using UnionIter = typename decltype(union_times)::const_iterator;

  // Find the union times iterator for a given time.
  const auto union_step = [&union_times, &less](const Time& t) {
    return std::lower_bound(union_times.cbegin(), union_times.cend(), t, less);
  };

  // The union time index for the step start.
  const auto union_step_start = union_step(start_time);

  // min(union_times.end(), it + order_s) except being careful not
  // to create out-of-range iterators.
  const auto advance_within_step = [order_s,
                                    &union_times](const UnionIter& it) {
    return union_times.end() - it >
                   static_cast<typename decltype(union_times)::difference_type>(
                       order_s)
               ? it + static_cast<typename decltype(
                          union_times)::difference_type>(order_s)
               : union_times.end();
  };

  // Calculating the Adams-Bashforth coefficients is somewhat
  // expensive, so we cache them.  ab_coefs(it, step) returns the
  // coefficients used to step from *it to *it + step.
  auto ab_coefs = make_overloader(
      make_cached_function<std::tuple<UnionIter, TimeDelta>, std::map>(
          [order_s](const std::tuple<UnionIter, TimeDelta>& args) {
            return get_coefficients(
                std::get<0>(args) -
                    static_cast<typename UnionIter::difference_type>(order_s -
                                                                     1),
                std::get<0>(args) + 1, std::get<1>(args));
          }),
      make_cached_function<std::tuple<UnionIter, ApproximateTimeDelta>,
                           std::map>(
          [order_s](const std::tuple<UnionIter, ApproximateTimeDelta>& args) {
            return get_coefficients(
                std::get<0>(args) -
                    static_cast<typename UnionIter::difference_type>(order_s -
                                                                     1),
                std::get<0>(args) + 1, std::get<1>(args));
          }));

  // The value of the coefficient of `evaluation_step` when doing
  // a standard Adams-Bashforth integration over the union times
  // from `step` to `step + 1`.
  const auto base_summand = [&ab_coefs, &end_time, &union_times](
                                const UnionIter& step,
                                const UnionIter& evaluation_step) {
    if (step + 1 != union_times.end()) {
      const TimeDelta step_size = *(step + 1) - *step;
      return step_size.value() *
             ab_coefs(std::make_tuple(
                 step, step_size))[static_cast<size_t>(step - evaluation_step)];
    } else {
      const auto step_size = end_time - *step;
      return step_size.value() *
             ab_coefs(std::make_tuple(
                 step, step_size))[static_cast<size_t>(step - evaluation_step)];
    }
  };

  for (auto local_evaluation_step = local_begin;
       local_evaluation_step != coupling.local_end();
       ++local_evaluation_step) {
    const auto union_local_evaluation_step = union_step(*local_evaluation_step);
    for (auto remote_evaluation_step = remote_begin;
         remote_evaluation_step != coupling.remote_end();
         ++remote_evaluation_step) {
      double deriv_coef = 0.;

      if (*local_evaluation_step == *remote_evaluation_step) {
        // The two elements stepped at the same time.  This gives a
        // standard Adams-Bashforth contribution to each segment
        // making up the current step.
        const auto union_step_upper_bound =
            advance_within_step(union_local_evaluation_step);
        for (auto step = union_step_start;
             step < union_step_upper_bound;
             ++step) {
          deriv_coef += base_summand(step, union_local_evaluation_step);
        }
      } else {
        // In this block we consider a coupling evaluation that is not
        // performed at equal times on the two sides of the mortar.

        // Makes an iterator with a map to give time as a double.
        const auto make_lagrange_iterator = [](const auto& it) {
          return boost::make_transform_iterator(
              it, [](const Time& t) { return t.value(); });
        };

        const auto union_remote_evaluation_step =
            union_step(*remote_evaluation_step);
        const auto union_step_lower_bound =
            std::max(union_step_start, union_remote_evaluation_step);

        // Compute the contribution to an interpolation over the local
        // times to `remote_evaluation_step->value()`, which we will
        // use as the coupling value for that time.  If there is an
        // actual evaluation at that time then skip this because the
        // Lagrange polynomial will be zero.
        if (not std::binary_search(local_begin, coupling.local_end(),
                                   *remote_evaluation_step, less)) {
          const auto union_step_upper_bound =
              advance_within_step(union_remote_evaluation_step);
          for (auto step = union_step_lower_bound;
               step < union_step_upper_bound;
               ++step) {
            deriv_coef += base_summand(step, union_remote_evaluation_step);
          }
          deriv_coef *=
              lagrange_polynomial(make_lagrange_iterator(local_evaluation_step),
                                  remote_evaluation_step->value(),
                                  make_lagrange_iterator(local_begin),
                                  make_lagrange_iterator(coupling.local_end()));
        }

        // Same qualitative calculation as the previous block, but
        // interpolating over the remote times.  This case is somewhat
        // more complicated because the latest remote time that can be
        // used varies for the different segments making up the step.
        if (not std::binary_search(remote_begin, coupling.remote_end(),
                                   *local_evaluation_step, less)) {
          auto union_step_upper_bound =
              advance_within_step(union_local_evaluation_step);
          if (coupling.remote_end() - remote_evaluation_step > order_s) {
            union_step_upper_bound = std::min(
                union_step_upper_bound,
                union_step(*(remote_evaluation_step + order_s)));
          }

          auto control_points = make_lagrange_iterator(
              remote_evaluation_step - remote_begin >= order_s
                  ? remote_evaluation_step - (order_s - 1)
                  : remote_begin);
          for (auto step = union_step_lower_bound;
               step < union_step_upper_bound;
               ++step, ++control_points) {
            deriv_coef +=
                base_summand(step, union_local_evaluation_step) *
                lagrange_polynomial(
                    make_lagrange_iterator(remote_evaluation_step),
                    local_evaluation_step->value(), control_points,
                    control_points +
                        static_cast<typename decltype(
                            control_points)::difference_type>(order_s));
          }
        }
      }

      if (deriv_coef != 0.) {
        // Skip the (potentially expensive) coupling calculation if
        // the coefficient is zero.
        *result += deriv_coef *
                   *coupling(local_evaluation_step, remote_evaluation_step);
      }
    }  // for remote_evaluation_step
  }  // for local_evaluation_step
}

template <typename Iterator, typename Delta>
std::vector<double> AdamsBashforthN::get_coefficients(
    const Iterator& times_begin, const Iterator& times_end, const Delta& step) {
  if (times_begin == times_end) {
    return {};
  }
  std::vector<double> steps;
  // This may be slightly more space than we need, but we can't get
  // the exact amount without iterating through the iterators, which
  // is not necessarily cheap depending on the iterator type.
  steps.reserve(maximum_order);
  for (auto t = times_begin; std::next(t) != times_end; ++t) {
    steps.push_back((*std::next(t) - *t).value());
  }
  steps.push_back(step.value());
  return get_coefficients_impl(steps);
}
}  // namespace TimeSteppers
