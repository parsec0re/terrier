#pragma once
#include "execution/compiler/expression/expression_translator.h"

namespace tpl::compiler {

/**
 * Null Check Transalator
 */
class NullCheckTranslator : public ExpressionTranslator {
 public:
  NullCheckTranslator(const terrier::parser::AbstractExpression *expression, CompilationContext *context);

  ast::Expr *DeriveExpr(const terrier::parser::AbstractExpression *expression, RowBatch *row) override;
};
}  // namespace tpl::compiler
