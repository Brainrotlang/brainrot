/* visitor.c - Generic AST traversal implementation */

#include "visitor.h"
#include "ast.h"

void ast_accept(ASTNode *node, Visitor *visitor)
{
    if (!node || !visitor)
        return;

    switch (node->type)
    {
    case NODE_INT:
        if (visitor->visit_int_literal)
            visitor->visit_int_literal(visitor, node);
        break;

    case NODE_FLOAT:
        if (visitor->visit_float_literal)
            visitor->visit_float_literal(visitor, node);
        break;

    case NODE_DOUBLE:
        if (visitor->visit_double_literal)
            visitor->visit_double_literal(visitor, node);
        break;

    case NODE_CHAR:
        if (visitor->visit_char_literal)
            visitor->visit_char_literal(visitor, node);
        break;

    case NODE_SHORT:
        if (visitor->visit_short_literal)
            visitor->visit_short_literal(visitor, node);
        break;

    case NODE_BOOLEAN:
        if (visitor->visit_boolean_literal)
            visitor->visit_boolean_literal(visitor, node);
        break;

    case NODE_STRING:
    case NODE_STRING_LITERAL:
        if (visitor->visit_string_literal)
            visitor->visit_string_literal(visitor, node);
        break;

    case NODE_IDENTIFIER:
        if (visitor->visit_identifier)
            visitor->visit_identifier(visitor, node);
        break;

    case NODE_OPERATION:
        // Visit children first, then the operation
        visit_children(node, visitor);
        if (visitor->visit_binary_operation)
            visitor->visit_binary_operation(visitor, node);
        break;

    case NODE_UNARY_OPERATION:
        // For increment/decrement operations, don't visit operand first to
        // avoid double evaluation
        if (node->data.unary.op != OP_POST_INC &&
            node->data.unary.op != OP_PRE_INC &&
            node->data.unary.op != OP_POST_DEC &&
            node->data.unary.op != OP_PRE_DEC)
        {
            // Visit operand first only for non-side-effect operations
            if (node->data.unary.operand)
                ast_accept(node->data.unary.operand, visitor);
        }
        if (visitor->visit_unary_operation)
            visitor->visit_unary_operation(visitor, node);
        break;

    case NODE_ARRAY_ACCESS:
        // `foo.arr[i]` (Array.base set, ast.h) -- visit the struct_access
        // base itself so it gets NODE_STRUCT_ACCESS's own handling,
        // exactly as if `foo.arr` had been used standalone. NULL for the
        // classic IDENTIFIER form, where there is no separate expression
        // to visit (the name is looked up directly, not walked).
        if (node->data.array.base)
            ast_accept(node->data.array.base, visitor);
        // Visit array indices first
        if (node->data.array.num_dimensions > 0)
        {
            for (int i = 0; i < node->data.array.num_dimensions; i++)
            {
                if (node->data.array.indices[i])
                    ast_accept(node->data.array.indices[i], visitor);
            }
        }
        else if (node->data.array.index)
        {
            ast_accept(node->data.array.index, visitor);
        }
        if (visitor->visit_array_access)
            visitor->visit_array_access(visitor, node);
        break;

    case NODE_FUNC_CALL:
    {
        if (visitor->visit_function_call)
            visitor->visit_function_call(visitor, node);
        break;
    }

    case NODE_SIZEOF:
        /* sizeof inspects operand type/shape without evaluating it.
           interpreter_visit_sizeof calls handle_sizeof() and must not
           auto-walk the operand (that would execute dereferences/calls).
           Semantic analysis leaves visit_sizeof NULL and walks NODE_SIZEOF
           itself in semantic_analyze_with_scope_tracking(). Other ast_accept
           consumers still recurse so they can inspect the operand. */
        if (visitor->visit_sizeof)
            visitor->visit_sizeof(visitor, node);
        else if (node->data.sizeof_stmt.expr)
            ast_accept(node->data.sizeof_stmt.expr, visitor);
        break;

    case NODE_DECLARATION:
    {
        /* Pre-visit the initializer -- but only where doing so does not
           evaluate it. visit_declaration evaluates the initializer for
           real straight afterwards, so any node this walk computes gets
           computed twice: `rizz x = a[f()]` ran f() twice that way, and
           an increment initializer stepped twice.
           ast_accept_evaluates_expression() is the authoritative list of
           the shapes this walk computes; it replaced an inc/dec-only check
           here that missed array access and nested assignment. */
        if (node->data.op.right &&
            !ast_accept_evaluates_expression(node->data.op.right))
            ast_accept(node->data.op.right, visitor);
        if (visitor->visit_declaration)
            visitor->visit_declaration(visitor, node);
        break;
    }

    case NODE_ASSIGNMENT:
    {
        /* Same rule as NODE_DECLARATION above: visit_assignment evaluates
           the right-hand side for real, so skip the pre-visit for every
           shape this walk would evaluate itself. */
        if (node->data.op.right &&
            !ast_accept_evaluates_expression(node->data.op.right))
            ast_accept(node->data.op.right, visitor);
        if (visitor->visit_assignment)
            visitor->visit_assignment(visitor, node);
        break;
    }

    case NODE_IF_STATEMENT:
        // Let the visitor handle the if statement logic
        if (visitor->visit_if_statement)
            visitor->visit_if_statement(visitor, node);
        break;

    case NODE_FOR_STATEMENT:
        // Let the visitor handle for statement logic
        if (visitor->visit_for_statement)
            visitor->visit_for_statement(visitor, node);
        break;

    case NODE_WHILE_STATEMENT:
        // Let the visitor handle while statement logic
        if (visitor->visit_while_statement)
            visitor->visit_while_statement(visitor, node);
        break;

    case NODE_DO_WHILE_STATEMENT:
        // Let the visitor handle do-while statement logic
        if (node->data.while_stmt.cond)
            ast_accept(node->data.while_stmt.cond, visitor);
        if (visitor->visit_do_while_statement)
            visitor->visit_do_while_statement(visitor, node);
        break;

    case NODE_SWITCH_STATEMENT:
        // Let the visitor handle switch statement logic
        if (visitor->visit_switch_statement)
            visitor->visit_switch_statement(visitor, node);
        break;

    case NODE_BREAK_STATEMENT:
        if (visitor->visit_break_statement)
            visitor->visit_break_statement(visitor, node);
        break;

    case NODE_RETURN:
        /* A bare call as the return expression (`bussin make();`) is NOT
           pre-walked here: this generic pre-visit would run interpreter_
           visit_function_call() (which executes a user-defined call and
           frees its result), and then handle_return_statement() -- the
           real evaluation -- would execute the SAME call a second time,
           duplicating its side effects (PR #254 review, finding 1). Every
           declared return type's arm in handle_return_statement() executes
           the return expression exactly once (the value-bearing arms via
           evaluate_expression_*; the void/NONE arm runs it explicitly for
           its side effects, dispatching native vs. user-defined the same
           way every other bare-call site does), so skipping the pre-visit
           for a bare call makes it run once, matching how a copy-init call
           (`gang Point c = make();`, on struct_init_expr) already avoids
           the double-run. Other return-expression shapes (`bussin a + b;`,
           `bussin p;`) still get the pre-visit. */
        if (node->data.op.left && node->data.op.left->type != NODE_FUNC_CALL)
            ast_accept(node->data.op.left, visitor);
        if (visitor->visit_return_statement)
            visitor->visit_return_statement(visitor, node);
        break;

    case NODE_FUNCTION_DEF:
        // DON'T visit body during definition - only when function is called
        if (visitor->visit_function_definition)
            visitor->visit_function_definition(visitor, node);
        break;

    case NODE_STATEMENT_LIST:
    {
        // Let the visitor handle the statement list - don't auto-traverse
        if (visitor->visit_statement_list)
            visitor->visit_statement_list(visitor, node);
        break;
    }

    case NODE_PRINT_STATEMENT:
        if (node->data.op.left)
            ast_accept(node->data.op.left, visitor);
        if (visitor->visit_print_statement)
            visitor->visit_print_statement(visitor, node);
        break;

    case NODE_ERROR_STATEMENT:
        if (node->data.op.left)
            ast_accept(node->data.op.left, visitor);
        if (visitor->visit_error_statement)
            visitor->visit_error_statement(visitor, node);
        break;

    case NODE_STRUCT_DEF:
        break;

    case NODE_STRUCT_ACCESS:
        /* Visit the object sub-expression */
        if (node->data.struct_access.object)
            ast_accept(node->data.struct_access.object, visitor);
        /* No dedicated visitor hook needed — access is handled by
           evaluate_struct_member_address at evaluation time.         */
        break;

    default:
        // Unknown node type - just continue
        break;
    }
}

void visit_children(ASTNode *node, Visitor *visitor)
{
    if (!node)
        return;

    switch (node->type)
    {
    case NODE_OPERATION:
        if (node->data.op.left)
            ast_accept(node->data.op.left, visitor);
        if (node->data.op.right)
            ast_accept(node->data.op.right, visitor);
        break;
    default:
        // Other node types handle their children in ast_accept
        break;
    }
}
