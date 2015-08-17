#include "strength_reducer.h"

#include "clang/AST/Expr.h"

#include "third_party/assert_exception.h"

#include "clang_utility_functions.h"
#include "pkt_func_transform.h"

using namespace clang;

std::string strength_reducer_transform(const TranslationUnitDecl * tu_decl) {
  return pkt_func_transform(tu_decl, strength_reduce_body); 
}

std::pair<std::string, std::vector<std::string>> strength_reduce_body(const CompoundStmt * function_body, const std::string & pkt_name __attribute__((unused))) {
  // Rewrite function body
  assert_exception(function_body);
  std::string transformed_body = "";
  for (const auto & child : function_body->children()) {
    assert_exception(isa<BinaryOperator>(child));
    const auto * bin_op = dyn_cast<BinaryOperator>(child);
    assert_exception(bin_op->isAssignmentOp());

    // Strip off parenthesis and casts for RHS
    const auto * rhs = bin_op->getRHS()->IgnoreParenImpCasts();

    if(not isa<ConditionalOperator>(rhs)) {
      // If it isn't a conditional operator, pass it through
      transformed_body += clang_stmt_printer(bin_op) + ";";
    } else {
      const auto * cond_op = dyn_cast<ConditionalOperator>(rhs);

      // Get condition expression
      const auto * cond = cond_op->getCond()->IgnoreParenImpCasts();

      if(isa<IntegerLiteral>(cond)) {
        // TODO: Check that it's the integer literal one
        // replace with straight-up assignment
        transformed_body += clang_stmt_printer(bin_op->getLHS()) + " = " + clang_stmt_printer(cond_op->getTrueExpr()) + ";"; 
      } else if (isa<BinaryOperator>(cond)) {
        // Has to be an &&, otherwise assert
        const auto * cond_predicate = dyn_cast<BinaryOperator>(cond);
        assert_exception(BinaryOperator::getOpcodeStr(cond_predicate->getOpcode()) == "&&");

        // Assuming it's &&, get left and right components
        const auto * left_bool_op = cond_predicate->getLHS();
        const auto * right_bool_op = cond_predicate->getRHS();

        // If both left_bool_op and right_bool_op are IntegerLiteral, something's wrong
        assert_exception(not (isa<IntegerLiteral>(left_bool_op) and isa<IntegerLiteral>(right_bool_op)));

        // If either of the two is an IntegerLiteral, return the other
        // TODO: Check that it's actually 1.
        const Expr * simplified_pred = nullptr;
        if (isa<IntegerLiteral>(left_bool_op)) {
          simplified_pred = right_bool_op;
        } else if (isa<IntegerLiteral>(right_bool_op)) {
          simplified_pred = left_bool_op;
        } else {
          simplified_pred = cond_predicate;
        }

        transformed_body += clang_stmt_printer(bin_op->getLHS()) + " = " + clang_stmt_printer(simplified_pred)
                                                       + " ? " + clang_stmt_printer(cond_op->getTrueExpr())
                                                       + " : " + clang_stmt_printer(cond_op->getFalseExpr()) + ";";
      } else if (isa<DeclRefExpr>(cond) or isa<MemberExpr>(cond)) {
        // pass it through, nothing to simplify here
        transformed_body += clang_stmt_printer(bin_op) + ";";
      } else {
        assert_exception(false);
      }
    }
  }
  return std::make_pair(transformed_body, std::vector<std::string>());
}
