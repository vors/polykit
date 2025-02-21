#include "delta.h"

#include "absl/container/flat_hash_set.h"

#include "util.h"


std::string dump_to_string_impl(const Delta& d) {
  return fmt::brackets(absl::StrCat(to_string(d.a()), ",", to_string(d.b())));
}

std::string to_string(const Delta& d) {
  HSpacing hspacing = *current_formatting_config().compact_x ? HSpacing::dense : HSpacing::sparse;
  SWITCH_ENUM_OR_DIE(d.b().form(), {
    case XForm::var:
    case XForm::sq_var:
    case XForm::infinity:
      return fmt::parens(fmt::diff(to_string(d.a()), to_string(d.b()), hspacing));
    case XForm::neg_var:
      return fmt::parens(fmt::sum(to_string(d.a()), to_string(-d.b()), hspacing));
    case XForm::zero: {
      // Add padding to preserve columns in a typical case
      const std::string a_str = to_string(d.a());
      const int reference_width = strlen_utf8(fmt::parens(fmt::diff(a_str, a_str, hspacing)));
      return pad_right(fmt::parens(a_str), reference_width);
    }
    case XForm::undefined:
      break;
  });
}


DeltaAlphabetMapping delta_alphabet_mapping;

X DeltaAlphabetMapping::alphabet_to_x(int ch) {
  CHECK_LE(0, ch);
  if (ch < kVarCodeEnd) {
    return X(XForm::var, ch - kVarCodeStart + 1);
  } else if (ch < kNegVarCodeEnd) {
    return X(XForm::neg_var, ch - kNegVarCodeStart + 1);
  } else if (ch == kZeroCode) {
    return Zero;
  } else {
    FATAL(absl::StrCat("Unexpected character: ", ch));
  }
}

DeltaAlphabetMapping::DeltaAlphabetMapping() {
  static constexpr int kAlphabetSize = kMaxDimension * (kMaxDimension - 1) / 2;
  static_assert(kAlphabetSize <= std::numeric_limits<internal::DeltaDiffT>::max() + 1);
  deltas_.resize(kAlphabetSize);
  for (int b : range(0, kMaxDimension)) {
    for (int a : range(0, b)) {
      Delta d(alphabet_to_x(a), alphabet_to_x(b));
      deltas_.at(to_alphabet(d)) = d;
    }
  }
}


static int num_distinct_variables(const std::vector<Delta>& term) {
  std::vector<int> elements;
  for (const Delta& d : term) {
    if (!d.a().is_constant()) {
      elements.push_back(d.a().idx());
    }
    if (!d.b().is_constant()) {
      elements.push_back(d.b().idx());
    }
  }
  return num_distinct_elements_unsorted(elements);
}


static X substitution_result(X orig, const std::vector<X>& new_points) {
  SWITCH_ENUM_OR_DIE_WITH_CONTEXT(orig.form(), "variable substitution", {
    case XForm::var:
      return new_points.at(orig.idx() - 1);
    case XForm::neg_var:
      return new_points.at(orig.idx() - 1).negated();
    case XForm::sq_var:
      break;
    case XForm::zero:
    case XForm::infinity:
      return orig;
    case XForm::undefined:
      break;
  });
}

DeltaExpr substitute_variables(const DeltaExpr& expr, const XArgs& new_points_arg) {
  const auto& new_points = new_points_arg.as_x();
  return expr.mapped_expanding([&](const DeltaExpr::ObjectT& term_old) -> DeltaExpr {
    std::vector<Delta> term_new;
    for (const Delta& d_old : term_old) {
      Delta d_new(
        substitution_result(d_old.a(), new_points),
        substitution_result(d_old.b(), new_points)
      );
      if (d_new.is_nil()) {
        return {};
      }
      term_new.push_back(d_new);
    }
    return DeltaExpr::single(term_new);
  }).without_annotations();
}

// TODO: Adopt optimized implementation to support X forms.
#if 0
#if DISABLE_PACKING
// ...
#else
DeltaExpr substitute_variables(const DeltaExpr& expr, const XArgs& new_points_arg) {
  constexpr int kMaxChar = std::numeric_limits<unsigned char>::max();
  constexpr int kNoReplacement = kMaxChar;
  constexpr int kNil = kMaxChar - 1;
  const auto& new_points = new_points_arg.as_x();
  std::array<unsigned char, kMaxChar> replacements;
  replacements.fill(kNoReplacement);
  const int num_src_vars = new_points.size();
  for (int a : range_incl(1, num_src_vars)) {
    for (int b : range_incl(a+1, num_src_vars)) {
      const Delta before = Delta(a, b);
      const Delta after = Delta(new_points[a-1], new_points[b-1]);
      const unsigned char key_before = delta_alphabet_mapping.to_alphabet(before);
      if (after.is_nil()) {
        replacements.at(key_before) = kNil;
      } else {
        const unsigned char key_after = delta_alphabet_mapping.to_alphabet(after);
        CHECK(key_after != kNoReplacement);
        CHECK(key_after != kNil);
        replacements.at(key_before) = key_after;
      }
    }
  }
  DeltaExpr ret;
  expr.foreach_key([&](const DeltaExpr::StorageT& term_old, int coeff) {
    DeltaExpr::StorageT term_new;
    for (unsigned char ch : term_old) {
      unsigned char ch_new = replacements.at(ch);
      CHECK(ch_new != kNoReplacement);
      if (ch_new == kNil) {
        return;
      }
      term_new.push_back(ch_new);
    }
    ret.add_to_key(term_new, coeff);
  });
  return ret;
}
#endif
#endif

DeltaExpr involute(const DeltaExpr& expr, const std::vector<int>& points) {
  return expr.mapped_expanding([&](const std::vector<Delta>& term) {
    return tensor_product(absl::MakeConstSpan(
      mapped(term, [&](const Delta& d) -> DeltaExpr {
        const auto [p1, p2, p3, p4, p5, p6] = to_array<6>(points);
        if (d == Delta(p6,p5)) {
          return D(p6,p1) - D(p1,p2) + D(p2,p3) - D(p3,p4) + D(p4,p5);
        } else if (d == Delta(p6,p4)) {
          return D(p4,p2) + D(p3,p1) - D(p1,p5) + D(p6,p1) - D(p1,p2) - D(p3,p4) + D(p4,p5);
        } else if (d == Delta(p6,p2)) {
          return D(p6,p1) - D(p1,p5) + D(p5,p3) - D(p3,p4) + D(p4,p2);
        } else {
          return DeltaExpr::single({d});
        }
      })
    ));
  });
}

DeltaExpr sort_term_multiples(const DeltaExpr& expr) {
  return expr.mapped([&](const std::vector<Delta>& term) {
    return sorted(term);
  });
}

DeltaExpr terms_with_unique_muptiples(const DeltaExpr& expr) {
  return expr.filtered([&](const std::vector<Delta>& term) {
    const auto term_sorted = sorted(term);
    return absl::c_adjacent_find(term_sorted) == term_sorted.end();
  });
}

DeltaExpr terms_with_nonunique_muptiples(const DeltaExpr& expr) {
  return expr.filtered([&](const std::vector<Delta>& term) {
    const auto term_sorted = sorted(term);
    return absl::c_adjacent_find(term_sorted) != term_sorted.end();
  });
}


DeltaExpr terms_with_num_distinct_variables(const DeltaExpr& expr, int num_distinct) {
  return expr.filtered([&](const std::vector<Delta>& term) {
    return num_distinct_variables(term) == num_distinct;
  });
}

DeltaExpr terms_with_min_distinct_variables(const DeltaExpr& expr, int min_distinct) {
  return expr.filtered([&](const std::vector<Delta>& term) {
    return num_distinct_variables(term) >= min_distinct;
  });
}

DeltaExpr terms_containing_only_variables(const DeltaExpr& expr, const std::vector<int>& indices) {
  absl::flat_hash_set<int> indices_set(indices.begin(), indices.end());
  return expr.filtered([&](const std::vector<Delta>& term) {
    return absl::c_all_of(term, [&](const Delta& d) {
      return indices_set.contains(d.a().idx()) && indices_set.contains(d.b().idx());
    });
  });
}

DeltaExpr terms_without_variables(const DeltaExpr& expr, const std::vector<int>& indices) {
  absl::flat_hash_set<int> indices_set(indices.begin(), indices.end());
  return expr.filtered([&](const std::vector<Delta>& term) {
    return !absl::c_any_of(term, [&](const Delta& d) {
      return indices_set.contains(d.a().idx()) && indices_set.contains(d.b().idx());
    });
  });
}


static bool between(int point, std::pair<int, int> segment) {
  const auto [a, b] = segment;
  CHECK_LT(a, b);
  ASSERT(all_unique_unsorted(std::array{point, a, b}));
  return a < point && point < b;
}

// TODO: Test.
bool are_weakly_separated(const Delta& d1, const Delta& d2) {
  if (d1.is_nil() || d2.is_nil()) {
    return true;
  }
  const int x1 = d1.a().as_simple_var();
  const int y1 = d1.b().as_simple_var();
  const int x2 = d2.a().as_simple_var();
  const int y2 = d2.b().as_simple_var();
  if (!all_unique_unsorted(std::array{x1, y1, x2, y2})) {
    return true;
  }
  const bool itersect = between(x1, {x2, y2}) != between(y1, {x2, y2});
  return !itersect;
}

// Optimization potential: consider whether this can be done in O(N) time;
bool is_weakly_separated(const DeltaExpr::ObjectT& term) {
  for (int i : range(term.size())) {
    for (int j : range(i)) {
      if (!are_weakly_separated(term[i], term[j])) {
        return false;
      }
    }
  }
  return true;
}
bool is_weakly_separated(const DeltaNCoExpr::ObjectT& term) {
  return is_weakly_separated(flatten(term));
}

bool is_totally_weakly_separated(const DeltaExpr& expr) {
  return !expr.contains([](const auto& term) { return !is_weakly_separated(term); });
}
bool is_totally_weakly_separated(const DeltaNCoExpr& expr) {
  return !expr.contains([](const auto& term) { return !is_weakly_separated(term); });
}

DeltaExpr keep_non_weakly_separated(const DeltaExpr& expr) {
  return expr.filtered([](const auto& term) { return !is_weakly_separated(term); });
}
DeltaNCoExpr keep_non_weakly_separated(const DeltaNCoExpr& expr) {
  return expr.filtered([](const auto& term) { return !is_weakly_separated(term); });
}

// TODO: Remove circular neighbour (n,1) when the number of points n is odd,
//   similarly to passes_normalize_remove_consecutive for GammaExpr.
bool passes_normalize_remove_consecutive(const DeltaExpr::ObjectT& term) {
  return absl::c_all_of(term, [](const Delta& d) {
    int a = d.a().as_simple_var();
    int b = d.b().as_simple_var();
    sort_two(a, b);
    return b != a + 1;
  });
}

DeltaExpr normalize_remove_consecutive(const DeltaExpr& expr) {
  return expr.filtered([](const auto& term) {
    return passes_normalize_remove_consecutive(term);
  });
}


static void graph_mark_reached(
    int start,
    const std::vector<std::vector<int>>& nbrs,
    std::vector<bool>& reached) {
  if (reached.at(start)) {
    return;
  }
  reached.at(start) = true;
  for (int v : nbrs.at(start)) {
    graph_mark_reached(v, nbrs, reached);
  }
}

static bool graph_is_connected(const std::vector<Delta>& deltas) {
  std::vector<std::pair<int, int>> graph;
  for (const Delta& d : deltas) {
    if (!d.a().is_constant() && !d.b().is_constant()) {
      graph.push_back({d.a().idx(), d.b().idx()});
    }
  }
  if (graph.empty()) {
    return true;
  }
  int max_vertex = -1;
  for (const auto& e : graph) {
    max_vertex = std::max({max_vertex, e.first, e.second});
  }
  std::vector<std::vector<int>> nbrs(max_vertex+1, std::vector<int>{});
  for (const auto& e : graph) {
    nbrs[e.first].push_back(e.second);
    nbrs[e.second].push_back(e.first);
  }
  std::vector<bool> reached(max_vertex+1, false);
  graph_mark_reached(graph.front().first, nbrs, reached);
  for (const auto& e : graph) {
    if (!reached[e.first] || !reached[e.second]) {
      return false;
    }
  }
  return true;
}

DeltaExpr terms_with_connected_variable_graph(const DeltaExpr& expr) {
  return expr.filtered([&](const std::vector<Delta>& term) {
    return graph_is_connected(term);
  });
}

int count_var(const DeltaExpr::ObjectT& term, int var) {
  return absl::c_count_if(term, [&](const Delta& d) {
    return d.a().idx() == var || d.b().idx() == var;
  });
};

void print_sorted_by_num_distinct_variables(std::ostream& os, const DeltaExpr& expr) {
  to_ostream_grouped(
    os, expr, std::less<>{},
    num_distinct_variables, std::less<>{},
    [](int num_vars) {
      return absl::StrCat(num_vars, " vars");
    },
    LinearNoContext{}
  );
}
