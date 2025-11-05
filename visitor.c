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
            // Visit operand first
            if (node->data.unary.operand)
                ast_accept(node->data.unary.operand, visitor);
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
            // Visit the initializer expression first
            if (node->data.op.right)
                ast_accept(node->data.op.right, visitor);
            if (visitor->visit_declaration)
                visitor->visit_declaration(visitor, node);
            break;
            
        case NODE_ASSIGNMENT:
            // Visit right side first
            if (node->data.op.right)
                ast_accept(node->data.op.right, visitor);
            if (visitor->visit_assignment)
                visitor->visit_assignment(visitor, node);
            break;
            
        case NODE_IF_STATEMENT:
            // Visit condition first, then branches
            if (node->data.if_stmt.condition)
                ast_accept(node->data.if_stmt.condition, visitor);
            if (node->data.if_stmt.then_branch)
                ast_accept(node->data.if_stmt.then_branch, visitor);
            if (node->data.if_stmt.else_branch)
                ast_accept(node->data.if_stmt.else_branch, visitor);
            if (visitor->visit_if_statement)
                visitor->visit_if_statement(visitor, node);
            break;
            
        case NODE_FOR_STATEMENT:
            // Visit all parts in order
            if (node->data.for_stmt.init)
                ast_accept(node->data.for_stmt.init, visitor);
            if (node->data.for_stmt.cond)
                ast_accept(node->data.for_stmt.cond, visitor);
            if (node->data.for_stmt.incr)
                ast_accept(node->data.for_stmt.incr, visitor);
            if (node->data.for_stmt.body)
                ast_accept(node->data.for_stmt.body, visitor);
            if (visitor->visit_for_statement)
                visitor->visit_for_statement(visitor, node);
            break;
            
        case NODE_WHILE_STATEMENT:
            if (node->data.while_stmt.cond)
                ast_accept(node->data.while_stmt.cond, visitor);
            if (node->data.while_stmt.body)
                ast_accept(node->data.while_stmt.body, visitor);
            if (visitor->visit_while_statement)
                visitor->visit_while_statement(visitor, node);
            break;
            
        case NODE_DO_WHILE_STATEMENT:
            if (node->data.while_stmt.body)
                ast_accept(node->data.while_stmt.body, visitor);
            if (node->data.while_stmt.cond)
                ast_accept(node->data.while_stmt.cond, visitor);
            if (visitor->visit_do_while_statement)
                visitor->visit_do_while_statement(visitor, node);
            break;
            
        case NODE_SWITCH_STATEMENT:
            // Visit expression first, then cases
            if (node->data.switch_stmt.expression)
                ast_accept(node->data.switch_stmt.expression, visitor);
            CaseNode *case_node = node->data.switch_stmt.cases;
            while (case_node) {
                if (case_node->value)
                    ast_accept(case_node->value, visitor);
                if (case_node->statements)
                    ast_accept(case_node->statements, visitor);
                case_node = case_node->next;
            }
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
            // Visit body
            if (node->data.function_def.body)
                ast_accept(node->data.function_def.body, visitor);
            if (visitor->visit_function_definition)
                visitor->visit_function_definition(visitor, node);
            break;
            
        case NODE_STATEMENT_LIST:
        {
            // Visit all statements in the list
            StatementList *stmt = node->data.statements;
            while (stmt) {
                if (stmt->statement)
                    ast_accept(stmt->statement, visitor);
                stmt = stmt->next;
            }
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
