#include "polylog_grli.h"

#include "itertools.h"


GammaExpr GrLiVec(const std::vector<int>& bonus_points, const std::vector<int>& main_points) {
  CHECK(main_points.size() % 2 == 0);
  const int weight = main_points.size() / 2;
  GammaExpr ret;
  for (const auto& [p, sign] : permutations_with_sign(main_points)) {
    const auto& permutation = p;  // workaround: lambdas cannot capture structured bindings
    ret.add_to(mapped(range(weight), [&](const auto& k) {
      return Gamma(concat(bonus_points, slice(permutation, k, k + weight)));
    }), sign);
  }
  return ret.annotate(
    fmt::function(
      fmt::opname("GrLi"),
      {str_join_skip_empty(
        std::array{
          str_join(sorted(bonus_points), ","),
          str_join(main_points, ",")
        },
        " / "
      )}
    )
  );
}
