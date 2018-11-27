#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "binder/sql_node_visitor.h"
#include "parser/expression/abstract_expression.h"
#include "parser/expression_defs.h"
#include "type/type_id.h"

namespace terrier::parser {

/**
 * Represents a logical tuple value.
 */
class TupleValueExpression : public AbstractExpression {
 public:
  /**
   * Creates a tuple value expression with the given column and table name.
   *
   */
  // TODO(WAN): I feel like this should be renamed. Maybe parameters reordered too.
  TupleValueExpression(std::string col_name, std::string table_name)
      : AbstractExpression(ExpressionType::VALUE_TUPLE, type::TypeId::INVALID, {}),
        col_name_(std::move(col_name)),
        table_name_(std::move(table_name)) {}

  std::unique_ptr<AbstractExpression> Copy() const override { return std::make_unique<TupleValueExpression>(*this); }

  void Accept(SqlNodeVisitor *v) override { v->Visit(this); }

 private:
  const std::string col_name_;
  const std::string table_name_;
};

}  // namespace terrier::parser
