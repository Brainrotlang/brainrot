/* visitor.c - Generic AST traversal implementation */

#include "visitor.h"
#include "ast.h"

void ast_accept(ASTNode *node, Visitor *visitor) {
    if (!node || !visitor) return;
    
    switch (node->type) {
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
            // For increment/decrement operations, don't visit operand first to avoid double evaluation
            if (node->data.unary.op != OP_POST_INC && node->data.unary.op != OP_PRE_INC && 
                node->data.unary.op != OP_POST_DEC && node->data.unary.op != OP_PRE_DEC) {
                // Visit operand first only for non-side-effect operations
                if (node->data.unary.operand)
                    ast_accept(node->data.unary.operand, visitor);
            }
            if (visitor->visit_unary_operation)
                visitor->visit_unary_operation(visitor, node);
            break;
            
        case NODE_ARRAY_ACCESS:
            // Visit array indices first
            if (node->data.array.num_dimensions > 0) {
                for (int i = 0; i < node->data.array.num_dimensions; i++) {
                    if (node->data.array.indices[i])
                        ast_accept(node->data.array.indices[i], visitor);
                }
            } else if (node->data.array.index) {
                ast_accept(node->data.array.index, visitor);
            }
            if (visitor->visit_array_access)
                visitor->visit_array_access(visitor, node);
            break;
            
        case NODE_FUNC_CALL:
        {
            // Visit arguments first
            ArgumentList *arg = node->data.func_call.arguments;
            while (arg) {
                if (arg->expr)
                    ast_accept(arg->expr, visitor);
                arg = arg->next;
            }
            if (visitor->visit_function_call)
                visitor->visit_function_call(visitor, node);
            break;
        }
            
        case NODE_SIZEOF:
            if (node->data.sizeof_stmt.expr)
                ast_accept(node->data.sizeof_stmt.expr, visitor);
            if (visitor->visit_sizeof)
                visitor->visit_sizeof(visitor, node);
            break;
            
        case NODE_DECLARATION:
            // For declarations with side-effect operations on the right, skip auto-visit to prevent double evaluation
            bool skip_decl_right_visit = false;
            if (node->data.op.right && node->data.op.right->type == NODE_UNARY_OPERATION) {
                OperatorType op = node->data.op.right->data.unary.op;
                if (op == OP_POST_INC || op == OP_PRE_INC || op == OP_POST_DEC || op == OP_PRE_DEC) {
                    skip_decl_right_visit = true;
                }
            }
            
            // Visit the initializer expression first (unless it's a side-effect operation)
            if (!skip_decl_right_visit && node->data.op.right)
                ast_accept(node->data.op.right, visitor);
            if (visitor->visit_declaration)
                visitor->visit_declaration(visitor, node);
            break;
            
        case NODE_ASSIGNMENT:
            // For assignments with side-effect operations on the right, skip auto-visit to prevent double evaluation
            bool skip_right_visit = false;
            if (node->data.op.right && node->data.op.right->type == NODE_UNARY_OPERATION) {
                OperatorType op = node->data.op.right->data.unary.op;
                if (op == OP_POST_INC || op == OP_PRE_INC || op == OP_POST_DEC || op == OP_PRE_DEC) {
                    skip_right_visit = true;
                }
            }
            
            // Visit right side first (unless it's a side-effect operation)
            if (!skip_right_visit && node->data.op.right)
                ast_accept(node->data.op.right, visitor);
            if (visitor->visit_assignment)
                visitor->visit_assignment(visitor, node);
            break;
            
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
            if (node->data.op.left)
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
            
        default:
            // Unknown node type - just continue
            break;
    }
}

void visit_children(ASTNode *node, Visitor *visitor) {
    if (!node) return;
    
    switch (node->type) {
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
