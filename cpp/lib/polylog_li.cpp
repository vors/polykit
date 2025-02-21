// Note: Cutting formulas are the same as in `iterated_integral`.
// Difference: Epsilon instead of Delta, `..._3_point` implementation.

#include "polylog_li.h"

#include "absl/types/span.h"

#include "algebra.h"
#include "check.h"
#include "format.h"
#include "sequence_iteration.h"


// Points:
//    0 => 0
//   -1 => 1
//    k => x_1 * ... * x_k  for  k >= 1

static constexpr int kZero = 0;
static constexpr int kOne = -1;

static constexpr inline bool is_var (int index) { return index >=  1; }
static constexpr inline bool is_zero(int index) { return index == kZero; }
static constexpr inline bool is_one (int index) { return index == kOne; }


std::vector<int> weights_to_dots(int foreweight, const std::vector<int>& weights) {
  std::vector<int> dots;
  for (EACH : range(foreweight + 1)) {
    dots.push_back(kZero);
  }
  dots.push_back(kOne);
  for (int i : range(weights.size())) {
    const int w = weights[i];
    const int p = i + 1;
    CHECK_GE(w, 1);
    CHECK_GE(p, 1);
    for (EACH : range(1, w)) {
      dots.push_back(kZero);
    }
    dots.push_back(p);
  }
  return dots;
}

LiParam dots_to_li_params(const std::vector<int>& dots_orig) {
  std::vector<int> dots = dots_orig;
  CHECK_GE(dots.size(), 3) << dump_to_string(dots_orig);
  CHECK(is_var(dots.back())) << dump_to_string(dots_orig);

  const int first_nonzero = absl::c_find_if_not(dots, is_zero) - dots.begin();
  CHECK_GT(first_nonzero, 0) << dump_to_string(dots_orig);

  int common_vars = 0;
  if (is_var(dots[first_nonzero])) {
    // Cancel common multipliers
    common_vars = dots[first_nonzero];
    dots[first_nonzero] = kOne;
    for (int i : range(2, dots.size())) {
      int& dot = dots[i];
      if (is_var(dot)) {
        dot -= common_vars;
      }
    }
  }

  std::vector<int> weights;
  std::vector<std::vector<int>> points;
  int cur_weight = 0;
  int prev_vars = 0;
  for (int i : range(first_nonzero + 1, dots.size())) {
    const int dot = dots[i];
    CHECK(!is_one(dot)) << dump_to_string(dots_orig);
    if (is_var(dot)) {
      ++cur_weight;
      weights.push_back(cur_weight);
      points.push_back(to_vector(range_incl(common_vars+prev_vars+1, common_vars+dot)));
      CHECK_LT(prev_vars, dot) << dump_to_string(dots_orig);
      prev_vars = dot;
      cur_weight = 0;
    } else {
      CHECK(is_zero(dot)) << dump_to_string(dots_orig);
      ++cur_weight;
    }
  }
  const int foreweight = first_nonzero - 1;
  return LiParam(foreweight, std::move(weights), std::move(points));
}

static EpsilonExpr EFormalSymbolSigned(const std::vector<int>& dots) {
  return EFormalSymbolSigned(dots_to_li_params(dots));
}

static EpsilonExpr EVarProd(int from, int to) {
  EpsilonExpr ret;
  for (int i : range_incl(from, to)) {
    ret += EVar(i);
  }
  return ret;
}

static EpsilonExpr Li_2_point_irreducible(int a, int b) {
  const int num_vars = is_var(a) + is_var(b);
  CHECK_LE(num_vars, 1);
  if (num_vars == 0) {
    return EpsilonExpr();
  }
  if (is_var(a)) {
    std::swap(a, b);
  }
  CHECK(!is_var(a) && is_var(b));
  if (is_zero(a)) {
    // x_1...x_b - 0
    return EVarProd(1, b);
  } else {
    CHECK(is_one(a));
    // x_1...x_b - 1
    return EComplementRangeInclusive(1, b);
  }
}

// Computes symbol for (p[2] - p[1]) / (p[1] - p[0]).
static EpsilonExpr Li_3_point(const absl::Span<const int>& p) {
  CHECK_EQ(p.size(), 3);
  int a = p[0];
  int b = p[1];
  int c = p[2];
  // Note. Point `b` is fixed. Points `a` and `c` can be swapped with
  // the change of result sign.
  const int num_vars = is_var(a) + is_var(b) + is_var(c);
  if (num_vars == 0) {
    return EpsilonExpr();
  } else if (num_vars == 1) {
    // If there is only one variable, the fraction is irreducible.
    return + Li_2_point_irreducible(c, b)
           - Li_2_point_irreducible(b, a);
  } else if (num_vars == 2) {
    if (!is_var(a)) {
      CHECK(!is_var(a) && is_var(b) && is_var(c));
      CHECK_LT(b, c);
      if (is_zero(a)) {
        // x_1...x_c - x_1...x_b
        // ----------------------  =  x_{b+1}...x_c - 1
        //     x_1...x_b - 0
        return EComplementRangeInclusive(b+1, c);
      } else {
        CHECK(is_one(a));
        // x_1...x_c - x_1...x_b      x_1 * ... * x_b * (x_{b+1}...x_c - 1)
        // ----------------------  =  -------------------------------------
        //     x_1...x_b - 1                      x_1...x_b - 1
        return + EVarProd(1, b)
               + EComplementRangeInclusive(b+1, c)
               - EComplementRangeInclusive(1, b);
      }
    } else if (!is_var(b)) {
      CHECK(is_var(a) && !is_var(b) && is_var(c));
      CHECK_LT(a, c);
      if (is_zero(b)) {
        // x_1...x_c - 0
        // -------------  =  x_{a+1} * ... * x_c
        // 0 - x_1...x_a
        return EVarProd(a+1, c);
      } else {
        CHECK(is_one(b));
        // x_1...x_c - 1
        // -------------
        // 1 - x_1...x_a
        return + EComplementRangeInclusive(1, c)
               - EComplementRangeInclusive(1, a);
      }
    } else {
      CHECK(is_var(a) && is_var(b) && !is_var(c));
      CHECK_LT(a, b);
      CHECK(is_zero(c));
      //     0 - x_1...x_b          x_{a+1} * ... * x_b
      // ----------------------  =  -------------------
      // x_1...x_b - x_1...x_a       x_{a+1}...x_b - 1
      return + EVarProd(a+1, b)
             - EComplementRangeInclusive(a+1, b);
    }
  } else if (num_vars == 3) {
    // x_1...x_c - x_1...x_b      x_{a+1} * ... * x_b * (x_{b+1}...x_c - 1)
    // ----------------------  =  -----------------------------------------
    // x_1...x_b - x_1...x_a                  x_{a+1}...x_b - 1
    return + EVarProd(a+1, b)
           + EComplementRangeInclusive(b+1, c)
           - EComplementRangeInclusive(a+1, b);
  } else {
    FATAL(absl::StrCat("Bad num_vars: ", num_vars));
  }
}

static EpsilonExpr Li_impl(const std::vector<int>& points) {
  const int num_points = points.size();
  CHECK_GE(num_points, 3);
  EpsilonExpr ret;
  if (num_points == 3) {
    ret = Li_3_point(absl::MakeConstSpan(points));
  } else {
    for (int i : range_incl(0, num_points - 3)) {
      ret += tensor_product(
        Li_impl(removed_index(points, i+1)),
        Li_3_point(absl::MakeConstSpan(points).subspan(i, 3))
      );
    }
  }
  return ret;
}

// Optimization potential: Add cache
EpsilonExpr LiVec(
    int foreweight,
    const std::vector<int>& weights,
    const std::vector<std::vector<int>>& points) {
  return LiVec(LiParam(foreweight, weights, points));
}

EpsilonExpr LiVec(const LiParam& param) {
  return substitute_variables(
      param.sign() * Li_impl(weights_to_dots(param.foreweight(), param.weights())),
      param.points()
    ).annotate(to_string(param));
}


EpsilonICoExpr CoLiVec(
    int foreweight,
    const std::vector<int>& weights,
    const std::vector<std::vector<int>>& points) {
  return CoLiVec(LiParam(foreweight, weights, points));
}

EpsilonICoExpr CoLiVec(const LiParam& param) {
  const std::vector<int> dots = weights_to_dots(param.foreweight(), param.weights());
  const int total_weight = dots.size() - 2;
  CHECK_EQ(total_weight, param.total_weight());

  EpsilonICoExpr ret;
  auto add_term = [&](const EpsilonExpr& lhs, const EpsilonExpr& rhs) {
    ret += icoproduct(
      substitute_variables(lhs, param.points()),
      substitute_variables(rhs, param.points())
    );
  };
  for (const std::vector<int>& seq_prototype : increasing_sequences(dots.size() - 2)) {
    if (seq_prototype.empty()) {
      continue;
    }
    std::vector<int> seq;
    seq.push_back(0);
    for (const int x : seq_prototype) {
      seq.push_back(x + 1);
    }
    seq.push_back(dots.size() - 1);

    bool two_formal_symbols_special_case = true;
    for (int i : range(1, seq.size() - 1)) {
      if (seq[i+1] - seq[i] != 1) {
        two_formal_symbols_special_case = false;
        break;
      }
    }

    if (two_formal_symbols_special_case) {
      const int cut = seq[1];
      if (is_var(dots[cut])) {
        const auto lower_dots = removed_slice(dots, 1, cut);
        const auto upper_dots = slice(dots, 0, cut+1);
        add_term(
          EFormalSymbolSigned(lower_dots),
          EFormalSymbolSigned(upper_dots)
        );
      }
    } else {
      bool term_is_zero = false;
      std::vector<EpsilonExpr> rhs_components;
      for (int i : range(seq.size() - 1)) {
        CHECK_LT(seq[i], seq[i+1]);
        if (seq[i+1] - seq[i] > 1) {
          if (is_zero(dots[seq[i]]) && is_zero(dots[seq[i+1]])) {
            term_is_zero = true;
            break;
          }
          rhs_components.push_back(Li_impl(slice(dots, seq[i], seq[i+1]+1)));
        }
      }
      if (!term_is_zero) {
        add_term(
          EFormalSymbolSigned(choose_indices(dots, seq)),
          shuffle_product_expr(absl::MakeConstSpan(rhs_components))
        );
      }
    }
  }
  ret += icoproduct(EUnity(), EFormalSymbolSigned(param));
  ret += icoproduct(EFormalSymbolSigned(param), EUnity());
  return (param.sign() * ret).annotate(fmt::comult() + to_string(param));
}
