#pragma once

#include "execution/compiler/operator/operator_translator.h"
#include "parser/expression/tuple_value_expression.h"

namespace tpl::compiler {

/**
 * SeqScan Translator
 */
class SeqScanTranslator : public OperatorTranslator {
 public:
  /**
   * Constructor
   * @param op plan node
   * @param pipeline current pipeline
   */
  SeqScanTranslator(const terrier::planner::AbstractPlanNode * op, CodeGen * codegen);

  void Produce(FunctionBuilder * builder) override;

  // Does nothing
  void InitializeStateFields(util::RegionVector<ast::FieldDecl *> *state_fields) override {}

  // Does nothing
  void InitializeStructs(util::RegionVector<ast::Decl *> *decls) override {}

  // Does nothing
  void InitializeHelperFunctions(util::RegionVector<ast::Decl *> *decls) override {}

  // Does nothing
  void InitializeSetup(util::RegionVector<ast::Stmt *> *setup_stmts) override {}

  // Does nothing
  void InitializeTeardown(util::RegionVector<ast::Stmt *> *teardown_stmts) override {}

  // Get an attribute by making a pci call
  ast::Expr* GetOutput(uint32_t attr_idx) override;

  // For a seq scan, this a pci call too
  ast::Expr* GetChildOutput(uint32_t child_idx, uint32_t attr_idx, terrier::type::TypeId type) override;

  // This is a materializer
  bool IsMaterializer(bool * is_ptr) override {
    *is_ptr = true;
    return true;
  }

  // Return the pci and its type
  std::pair<ast::Identifier*, ast::Identifier*> GetMaterializedTuple() override {
    return {&pci_, &pci_type_};
  }


 private:
  // var tvi : TableVectorIterator
  void DeclareTVI(FunctionBuilder * builder);

  // for (@tableIterInit(&tvi, ...); @tableIterAdvance(&tvi);) {...}
  void GenTVILoop(FunctionBuilder * builder);

  // var pci = @tableIterGetPCI(&tvi)
  // for (; @pciHasNext(pci); @pciAdvance(pci)) {...}
  void GenPCILoop(FunctionBuilder * builder);

  // if (cond) {...}
  void GenScanCondition(FunctionBuilder * builder);

  // @tableIterClose(&tvi)
  void GenTVIClose(FunctionBuilder * builder);

  // Whether the seq scan can be vectorized
  bool IsVectorizable(const terrier::parser::AbstractExpression * predicate);

  // Generated vectorized filters
  void GenVectorizedPredicate(FunctionBuilder * builder, const terrier::parser::AbstractExpression * predicate);

  // Whether there is a scan predicate.
  bool has_predicate_{false};

  // Structs, functions and locals
  static constexpr const char * tvi_name_ = "tvi";
  static constexpr const char * pci_name_ = "pci";
  static constexpr const char * row_name_ = "row";
  static constexpr const char * table_struct_name_= "TableRow";
  static constexpr const char * pci_type_name_ = "ProjectedColumnsIterator";
  ast::Identifier tvi_;
  ast::Identifier pci_;
  ast::Identifier row_;
  ast::Identifier table_struct_;
  ast::Identifier pci_type_;
};

}  // namespace tpl::compiler
