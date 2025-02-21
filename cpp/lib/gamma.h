// Represents a Grassmannian polylogarithm in Plücker coordinates.

#pragma once

#include <bitset>
#include <climits>

#include "bitset_util.h"
#include "check.h"
#include "coalgebra.h"
#include "delta.h"
#include "pvector.h"


constexpr int kMaxGammaVariables = 16;

static_assert(kMaxGammaVariables <= sizeof(unsigned long) * CHAR_BIT);  // for std::bitset::to_ulong


// Represents a minor [d * d] in a matrix [d * n], n > d.
// Indices correspond to columns included into the minor.
// Generalization of `Delta` (assuming the latter uses only XForm::var).
class Gamma {
public:
  using BitsetT = std::bitset<kMaxGammaVariables>;
  static constexpr int kBitsetOffset = 1;

  Gamma() {}
  explicit Gamma(BitsetT indices) : indices_(std::move(indices)) {}
  explicit Gamma(const std::vector<int>& vars);

  bool is_nil() const { return indices_.none(); }

  const BitsetT& index_bitset() const { return indices_; }
  std::vector<int> index_vector() const { return bitset_to_vector(indices_, kBitsetOffset); }

  bool operator==(const Gamma& other) const { return indices_ == other.indices_; }
  bool operator< (const Gamma& other) const { return indices_.to_ulong() < other.indices_.to_ulong(); }

  template <typename H>
  friend H AbslHashValue(H h, const Gamma& gamma) {
    return H::combine(std::move(h), gamma.indices_);
  }

private:
  BitsetT indices_;  // 0-based
};

inline Gamma::Gamma(const std::vector<int>& vars) {
  const auto indices_or = vector_to_bitset_or<BitsetT>(vars, kBitsetOffset);
  if (indices_or.has_value()) {
    indices_ = indices_or.value();
  } else {
    // Keep empty: this means Gamma is nil.
  }
}

std::string to_string(const Gamma& g);


namespace internal {
struct GammaExprParam {
  using ObjectT = std::vector<Gamma>;
  using StorageT = PVector<Gamma, 10>;
  static StorageT object_to_key(const ObjectT& obj) {
    return to_pvector<StorageT>(obj);
  }
  static ObjectT key_to_object(const StorageT& key) {
    return to_vector(key);
  }
  IDENTITY_VECTOR_FORM
  LYNDON_COMPARE_DEFAULT
  static std::string object_to_string(const ObjectT& obj) {
    return str_join(obj, fmt::tensor_prod());
  }
  static StorageT monom_tensor_product(const StorageT& lhs, const StorageT& rhs) {
    return concat(lhs, rhs);
  }
  static int object_to_weight(const ObjectT& obj) {
    return obj.size();
  }
  static int object_to_dimension(const ObjectT& obj) {
    CHECK(!obj.empty());
    const auto dimensions = mapped(obj, [](const Gamma& g) { return g.index_vector().size(); });
    CHECK(all_equal(dimensions)) << dump_to_string(obj);
    return dimensions.front();
  }
};

struct GammaICoExprParam {
  using PartExprParam = GammaExprParam;
  using ObjectT = std::vector<std::vector<Gamma>>;
  using PartStorageT = GammaExprParam::StorageT;
  using StorageT = PVector<PartStorageT, 2>;
  static StorageT object_to_key(const ObjectT& obj) {
    return mapped_to_pvector<StorageT>(obj, GammaExprParam::object_to_key);
  }
  static ObjectT key_to_object(const StorageT& key) {
    return mapped(key, GammaExprParam::key_to_object);
  }
  IDENTITY_VECTOR_FORM
  LYNDON_COMPARE_LENGTH_FIRST
  static std::string object_to_string(const ObjectT& obj) {
    return str_join(obj, fmt::coprod_iterated(), GammaExprParam::object_to_string);
  }
  static int object_to_weight(const ObjectT& obj) {
    return sum(mapped(obj, [](const auto& part) { return part.size(); }));
  }
  static int object_to_dimension(const ObjectT& obj) {
    CHECK(!obj.empty());
    const auto part_dimensions = mapped(obj, &GammaExprParam::object_to_dimension);
    CHECK(all_equal(part_dimensions));
    return part_dimensions.front();
  }
  static constexpr bool coproduct_is_lie_algebra = true;
  static constexpr bool coproduct_is_iterated = true;
};

struct GammaNCoExprParam : GammaICoExprParam {
  static std::string object_to_string(const ObjectT& obj) {
    return str_join(obj, fmt::coprod_normal(), GammaExprParam::object_to_string);
  }
  static constexpr bool coproduct_is_iterated = false;
};

struct GammaACoExprParam : GammaICoExprParam {
  static bool lyndon_compare(const VectorT::value_type& lhs, const VectorT::value_type& rhs) {
    // TODO: Consider whether this could be made the default order for co-expressions.
    //   If so, remove this additional co-expression type.
    using namespace cmp;
    return projected(lhs, rhs, [](const auto& v) {
      return std::tuple{desc_val(v.size()), asc_ref(v)};
    });
  };
};
}  // namespace internal


using GammaExpr = Linear<internal::GammaExprParam>;
using GammaICoExpr = Linear<internal::GammaICoExprParam>;
using GammaNCoExpr = Linear<internal::GammaNCoExprParam>;
using GammaACoExpr = Linear<internal::GammaACoExprParam>;
template<> struct ICoExprForExpr<GammaExpr> { using type = GammaICoExpr; };
template<> struct NCoExprForExpr<GammaExpr> { using type = GammaNCoExpr; };

inline GammaExpr G(const std::vector<int>& vars) {
  Gamma g(vars);
  return g.is_nil() ? GammaExpr{} : GammaExpr::single({g});
}


GammaExpr substitute_variables(const GammaExpr& expr, const std::vector<int>& new_points);

GammaExpr project_on(int axis, const GammaExpr& expr);

bool are_weakly_separated(const Gamma& g1, const Gamma& g2);
bool is_weakly_separated(const GammaExpr::ObjectT& term);
bool is_weakly_separated(const GammaNCoExpr::ObjectT& term);
bool is_totally_weakly_separated(const GammaExpr& expr);
bool is_totally_weakly_separated(const GammaNCoExpr& expr);
GammaExpr keep_non_weakly_separated(const GammaExpr& expr);
GammaNCoExpr keep_non_weakly_separated(const GammaNCoExpr& expr);

bool passes_normalize_remove_consecutive(const GammaExpr::ObjectT& term, int dimension, int num_points);
GammaExpr normalize_remove_consecutive(const GammaExpr& expr, int dimension, int num_points);
GammaExpr normalize_remove_consecutive(const GammaExpr& expr);

// Requires that each term is a simple variable difference, i.e. terms like (x_i + x_j)
// or (x_i + 0) are not allowed.
GammaExpr delta_expr_to_gamma_expr(const DeltaExpr& expr);

// Requires that expression is dimension 2.
DeltaExpr gamma_expr_to_delta_expr(const GammaExpr& expr);

GammaExpr pullback(const GammaExpr& expr, const std::vector<int>& bonus_points);
GammaExpr pullback(const DeltaExpr& expr, const std::vector<int>& bonus_points);

GammaExpr plucker_dual(const GammaExpr& expr, const std::vector<int>& point_universe);
GammaExpr plucker_dual(const DeltaExpr& expr, const std::vector<int>& point_universe);

// Converts each term
//     x1 * x2 * ... * xn
// into a sum
//   + (x1*x2) @ x3 @ ... @ xn
//   + x1 @ (x2*x3) @ ... @ xn
//     ...
//   + x1 @ x2 @ ... @ (x{n-1}*xn)
//
// Note: In principle this is a generic coalgebra operation that could be applied to
//   any expression type. However need to decide whether ACoExpr is a thing before
//   promoting this to coalgebra module.
//
GammaACoExpr expand_into_glued_pairs(const GammaExpr& expr);
