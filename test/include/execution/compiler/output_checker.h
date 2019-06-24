#include "util/test_harness.h"
#include "execution/sql/value.h"
#include "planner/plannodes/output_schema.h"
#include "type/type_id.h"

// TODO(Amadou): Currently all checker only work on single integer columns. Ideally, we want them to work on arbitrary expressions,
// but this is no simple task. We would basically need an expression evaluator on output rows.

namespace tpl::compiler {
/**
 * Helper class to check if the output of a query is corrected.
 */
class OutputChecker {
 public:
  virtual void CheckCorrectness() = 0;
  virtual void ProcessBatch(const std::vector<std::vector<sql::Val*>> & output) = 0;
};

/**
 * Runs multiples output checkers at once
 */
class MultiChecker : public OutputChecker {
 public:
  /**
   * Constructor
   * @param checkers list of output checkers
   */
  MultiChecker(std::vector<OutputChecker*> && checkers)
  : checkers_{std::move(checkers)} {}

  /**
   * Call checkCorrectness on all output checkers
   */
  void CheckCorrectness() override {
    for (const auto checker : checkers_) {
      checker->CheckCorrectness();
    }
  }

  /**
   * Calls all output checkers
   * @param output current output batch
   */
  void ProcessBatch(const std::vector<std::vector<sql::Val*>> & output) override {
    for (const auto checker : checkers_) {
      checker->ProcessBatch(output);
    }
  }

 private:
  // list of checkers
  std::vector<OutputChecker*> checkers_;
};

using RowChecker = std::function<void(const std::vector<sql::Val*> &)>;
using CorrectnessFn = std::function<void()>;
class GenericChecker : public OutputChecker {
 public:
  GenericChecker(RowChecker row_checker, CorrectnessFn correctness_fn) :
  row_checker_{row_checker}, correctness_fn_(correctness_fn) {}

  void CheckCorrectness() override {
    if (bool(correctness_fn_)) correctness_fn_();
  }

  void ProcessBatch(const std::vector<std::vector<sql::Val*>> & output) override {
    if (!bool(row_checker_)) return;
    for (const auto & vals: output) {
      row_checker_(vals);
    }
  }

 private:
  RowChecker row_checker_;
  CorrectnessFn correctness_fn_;
};

/**
 * Checks if the number of output tuples is correct
 */
class NumChecker : public OutputChecker {
 public:
  /**
   * Constructor
   * @param expected_count the expected number of output tuples
   */
  NumChecker(i64 expected_count) : expected_count_{expected_count} {}

  /**
   * Checks if the expected number and the received number are the same
   */
  void CheckCorrectness() override {
    EXPECT_EQ(curr_count_, expected_count_);
  }

  /**
   * Increment the current count
   * @param output current output batch
   */
  void ProcessBatch(const std::vector<std::vector<sql::Val*>> & output) override {
    curr_count_ += output.size();
  }

 private:
  // Current number of tuples
  i64 curr_count_{0};
  // Expected number of tuples
  i64 expected_count_;
};

/**
 * Checks that the values in a column satisfy a certain comparison.
 * @tparam CompFn comparison function to use
 */
class SingleIntComparisonChecker : public OutputChecker {
 public:
  SingleIntComparisonChecker(std::function<bool(i64, i64)> fn, uint32_t col_idx, i64 rhs) : comp_fn_(fn), col_idx_{col_idx}, rhs_{rhs} {}

  void CheckCorrectness() override {}

  void ProcessBatch(const std::vector<std::vector<sql::Val*>> & output) override {
    for (const auto & vals: output) {
      auto int_val = static_cast<const sql::Integer *>(vals[col_idx_]);
      EXPECT_TRUE(comp_fn_(int_val->val, rhs_));
    }
  }

 private:
  std::function<bool(i64, i64)> comp_fn_;
  uint32_t col_idx_;
  i64 rhs_;
};

/**
 * Checks if two joined columns are the same.
 */
class SingleIntJoinChecker : public OutputChecker {
 public:
  /**
   * Constructor
   * @param col1 first column of the join
   * @param col2 second column of the join
   */
  SingleIntJoinChecker(uint32_t col1, uint32_t col2) : col1_(col1), col2_(col2) {}

  /**
   * Does nothing. All the checks are done in ProcessBatch
   */
  void CheckCorrectness() override {}

  /**
   * Checks that the two joined columns are the same
   * @param output current output
   */
  void ProcessBatch(const std::vector<std::vector<sql::Val*>> & output) override {
    for (const auto & vals: output) {
      auto val1 = static_cast<const sql::Integer *>(vals[col1_]);
      auto val2 = static_cast<const sql::Integer *>(vals[col2_]);
      EXPECT_EQ(val1->val, val2->val);
    }
  }

 private:
  uint32_t col1_;
  uint32_t col2_;
};

/**
 * Checks that a columns sums up to an expected value
 */
class SingleIntSumChecker : public OutputChecker {
 public:
  /**
   * Constructor
   * @param col_idx index of column to sum
   * @param expected expected sum
   */
  SingleIntSumChecker(uint32_t col_idx, i64 expected) : col_idx_{col_idx}, expected_{expected} {}

  /**
   * Checks of the expected sum and the received sum are the same
   */
  void CheckCorrectness() {
    EXPECT_EQ(curr_sum_, expected_);
  }

  /**
   * Update the current sum
   * @param output current output batch
   */
  void ProcessBatch(const std::vector<std::vector<sql::Val*>> & output) override {
    for (const auto & vals: output) {
      auto int_val = static_cast<sql::Integer *>(vals[col_idx_]);
      if (!int_val->is_null) curr_sum_ += int_val->val;
    }
  }

 private:
  uint32_t col_idx_;
  i64 curr_sum_{0};
  i64 expected_;
};

/**
 * Checks that a given column is sorted
 */
class SingleIntSortChecker : public OutputChecker {
 public:
  /**
   * Constructor
   * @param col_idx column to check
   */
  SingleIntSortChecker(uint32_t col_idx) : col_idx_{0} {}

  /**
   * Does nothing. All the checking is done in ProcessBatch.
   */
  void CheckCorrectness() override {}

  /**
   * Compares each value with the previous one to make sure they are sorted
   * @param output current output batch.
   */
  void ProcessBatch(const std::vector<std::vector<sql::Val*>> & output) override {
    for (const auto &vals: output) {
      auto int_val = static_cast<sql::Integer *>(vals[col_idx_]);
      if (int_val->is_null) {
        EXPECT_TRUE(prev_val_.is_null);
      } else {
        EXPECT_TRUE(prev_val_.is_null || int_val->val >= prev_val_.val);
      }
      // Copy the value since the pointer does not belong to this class.
      prev_val_ = *int_val;
    }
  }

 private:
  sql::Integer prev_val_{true, 0};
  uint32_t col_idx_;
};

/**
 * Runs multiple OutputCallbacks at once
 */
class MultiOutputCallback {
 public:
  /**
   * Constructor
   * @param callbacks list of output callbacks
   */
  MultiOutputCallback(std::vector<exec::OutputCallback> callbacks) : callbacks_{std::move(callbacks)} {}

  /**
   * OutputCallback function
   */
  void operator()(byte *tuples, u32 num_tuples, u32 tuple_size) {
    for (auto & callback : callbacks_) {
      callback(tuples, num_tuples, tuple_size);
    }
  }
 private:
  std::vector<exec::OutputCallback> callbacks_;
};

/**
 * An output callback that stores the rows in a vector and runs a checker on them.
 */
class OutputStore {
 public:
  /**
   * Constructor
   * @param checker checker to run
   * @param schema output schema of the query.
   */
  OutputStore(OutputChecker * checker, const terrier::planner::OutputSchema * schema)
  : schema_(schema)
  , checker_(checker){}

  /**
   * OutputCallback function. This will gather the output in a vector.
   */
  void operator()(byte *tuples, u32 num_tuples, u32 tuple_size) {
    for (u32 row = 0; row < num_tuples; row++) {
      uint32_t curr_offset = 0;
      std::vector<sql::Val*> vals;
      for (u16 col = 0; col < schema_->GetColumns().size(); col++) {
        // TODO(Amadou): Figure out to print other types.
        switch (schema_->GetColumns()[col].GetType()) {
          case terrier::type::TypeId::TINYINT:
          case terrier::type::TypeId::SMALLINT:
          case terrier::type::TypeId::BIGINT:
          case terrier::type::TypeId::INTEGER: {
            auto *val = reinterpret_cast<sql::Integer *>(tuples + row * tuple_size + curr_offset);
            vals.emplace_back(val);
            break;
          }
          case terrier::type::TypeId::BOOLEAN: {
            auto *val = reinterpret_cast<sql::BoolVal *>(tuples + row * tuple_size + curr_offset);
            vals.emplace_back(val);
            break;
          }
          case terrier::type::TypeId::DECIMAL: {
            auto *val = reinterpret_cast<sql::Real *>(tuples + row * tuple_size + curr_offset);
            vals.emplace_back(val);
            break;
          }
          case terrier::type::TypeId::DATE: {
            auto *val = reinterpret_cast<sql::Date *>(tuples + row * tuple_size + curr_offset);
            vals.emplace_back(val);
            break;
          }
          case terrier::type::TypeId::VARCHAR: {
            auto *val = reinterpret_cast<sql::StringVal *>(tuples + row * tuple_size + curr_offset);
            vals.emplace_back(val);
            break;
          }
          default:
            UNREACHABLE("Cannot output unsupported type!!!");
        }
        curr_offset += sql::ValUtil::GetSqlSize(schema_->GetColumns()[col].GetType());
      }
      output.emplace_back(vals);
    }
    checker_->ProcessBatch(output);
    output.clear();
  }

 private:
  // Current output batch
  std::vector<std::vector<sql::Val*>> output;
  // output schema
  const terrier::planner::OutputSchema *schema_;
  // checker to run
  OutputChecker * checker_;
};
}
