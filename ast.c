/* ast.c */

#include "ast.h"
#include "lib/mem.h"
#include "lib/hm.h"
#include <stdbool.h>
#include <limits.h>
#include <float.h>
#include <stdint.h>

JumpBuffer *jump_buffer = {0};

Function *function_table = NULL;
jmp_buf return_jump_buf;
ReturnValue current_return_value;

TypeModifiers current_modifiers = {false, false, false, false, false};
extern VarType current_var_type;

Scope *current_scope;

// Symbol table functions
bool set_variable(const char *name, void *value, VarType type, TypeModifiers mods)
{
    Variable *var = get_variable(name);
    if (var != NULL)
    {
        var->modifiers = mods;
        var->var_type = type;
        switch (type)
        {
        case VAR_INT:
            var->value.ivalue = *(int *)value;
            break;
        case VAR_SHORT:
            var->value.svalue = *(short *)value;
            break;
        case VAR_FLOAT:
            var->value.fvalue = *(float *)value;
            break;
        case VAR_DOUBLE:
            var->value.dvalue = *(double *)value;
            break;
        case VAR_BOOL:
            var->value.bvalue = *(bool *)value;
            break;
        case VAR_CHAR:
            var->value.ivalue = *(char *)value;
            break;
        case NONE:
            break;
        default:
            break;
        }
        return true;
    }
    return false; // Symbol table is full
}

bool set_int_variable(const char *name, int value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_INT, mods);
}

bool set_array_variable(const char *name, int length, TypeModifiers mods, VarType type)
{
    // search for an existing variable
    Variable *var = get_variable(name);
    if (var != NULL)
    {
        if (var->is_array)
        {
            // free the old array
            switch (var->var_type)
            {
            case VAR_INT:
                SAFE_FREE(var->value.iarray);
                break;
            case VAR_SHORT:
                SAFE_FREE(var->value.sarray);
                break;
            case VAR_FLOAT:
                SAFE_FREE(var->value.farray);
                break;
            case VAR_DOUBLE:
                SAFE_FREE(var->value.darray);
                break;
            case VAR_BOOL:
                SAFE_FREE(var->value.barray);
                break;
            case VAR_CHAR:
                SAFE_FREE(var->value.carray);
                break;
            default:
                break;
            }
        }
        var->var_type = type;
        var->is_array = true;
        var->array_length = length;
        var->modifiers = mods;
        switch (type)
        {
        case VAR_INT:
            var->value.iarray = SAFE_MALLOC_ARRAY(int, length);
            if(length) memset(var->value.iarray, 0, length * sizeof(int));
            break;
        case VAR_SHORT:
            var->value.sarray = SAFE_MALLOC_ARRAY(short, length);
            if(length) memset(var->value.sarray, 0, length * sizeof(short));
            break;
        case VAR_FLOAT:
            var->value.farray = SAFE_MALLOC_ARRAY(float, length);
            if(length) memset(var->value.farray, 0, length * sizeof(float));
            break;
        case VAR_DOUBLE:
            var->value.darray = SAFE_MALLOC_ARRAY(double, length);
            if(length) memset(var->value.darray, 0, length * sizeof(double));
            break;
        case VAR_BOOL:
            var->value.barray = SAFE_MALLOC_ARRAY(bool, length);
            if(length) memset(var->value.barray, 0, length * sizeof(bool));
            break;
        case VAR_CHAR:
            var->value.carray = SAFE_MALLOC_ARRAY(char, length);
            if(length) memset(var->value.carray, 0, length * sizeof(char));
            break;
        default:
            break;
        }
        return true;
    }

    return false; // no space
}

bool set_short_variable(const char *name, short value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_SHORT, mods);
}

bool set_float_variable(const char *name, float value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_FLOAT, mods);
}

bool set_double_variable(const char *name, double value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_DOUBLE, mods);
}

bool set_bool_variable(const char *name, bool value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_BOOL, mods);
}

void reset_modifiers(void)
{
    current_modifiers.is_volatile = false;
    current_modifiers.is_signed = false;
    current_modifiers.is_unsigned = false;
    current_modifiers.is_const = false;
}

TypeModifiers get_current_modifiers(void)
{
    TypeModifiers mods = current_modifiers;
    reset_modifiers(); // Reset for next declaration
    return mods;
}

/* Include the symbol table functions */
extern void yyerror(const char *s);
extern void ragequit(int exit_code);
extern void chill(unsigned int seconds);
extern void yapping(const char *format, ...);
extern void yappin(const char *format, ...);
extern void baka(const char *format, ...);
extern char slorp_char(char chr);
extern char *slorp_string(char *string);
extern int slorp_int(int val);
extern short slorp_short(short val);
extern float slorp_float(float var);
extern double slorp_double(double var);

extern int yylineno;

/* Function implementations */

bool check_and_mark_identifier(ASTNode *node, const char *contextErrorMessage)
{
    if (!node->already_checked)
    {
        node->already_checked = true;
        node->is_valid_symbol = false;

        // Do the table lookup
        const Variable *var = get_variable(node->data.name);
        if (var != NULL) {
            node->is_valid_symbol = true;
}

        if (!node->is_valid_symbol)
        {
            yylineno = yylineno - 2;
            yyerror(contextErrorMessage);
        }
    }

    return node->is_valid_symbol;
}

void execute_switch_statement(ASTNode *node)
{
    int switch_value = evaluate_expression(node->data.switch_stmt.expression);
    CaseNode *current_case = node->data.switch_stmt.cases;

    PUSH_JUMP_BUFFER();
    if (setjmp(CURRENT_JUMP_BUFFER()) == 0)
    {
        int matched = 0;
        while (current_case)
        {
            if (current_case->value)
            {
                int case_value = evaluate_expression(current_case->value);
                if (case_value == switch_value || matched)
                {
                    matched = 1;
                    execute_statements(current_case->statements);
                }
            }
            else
            {
                // Default case
                execute_statements(current_case->statements);
                break;
            }
            current_case = current_case->next;
        }
    }
    else
    {
        // Break encountered; do nothing
    }
    POP_JUMP_BUFFER();
}

static ASTNode *create_node(NodeType type, VarType var_type, TypeModifiers modifiers)
{
    ASTNode *node = SAFE_MALLOC(ASTNode);
    if (!node)
    {
        yyerror("Error: Memory allocation failed for ASTNode.\n");
        exit(EXIT_FAILURE);
    }
    node->type = type;
    node->var_type = var_type;
    node->modifiers = modifiers;
    node->already_checked = false;
    node->is_valid_symbol = false;
    return node;
}

ASTNode *create_int_node(int value)
{
    ASTNode *node = create_node(NODE_INT, VAR_INT, current_modifiers);
    SET_DATA_INT(node, value);
    return node;
}

ASTNode *create_array_declaration_node(const char *name, int length, VarType var_type)
{
    ASTNode *node = SAFE_MALLOC(ASTNode);
    if (!node) {
        return NULL;
}

    node->type = NODE_ARRAY_ACCESS;
    node->var_type = var_type;
    node->is_array = true;
    node->array_length = length;
    node->data.array.name = safe_strdup(name);
    return node;
}

ASTNode *create_array_access_node(const char *name, ASTNode *index)
{
    ASTNode *node = SAFE_MALLOC(ASTNode);
    if (!node)
    {
        yyerror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    node->type = NODE_ARRAY_ACCESS;
    node->data.array.name = safe_strdup(name);
    node->data.array.index = index;
    node->is_array = true;

    // Look up and set the array's type from the symbol table
    const Variable *var = get_variable(name);
    if (var != NULL)
    {
        node->var_type = var->var_type;
        node->array_length = var->array_length;
        node->modifiers = var->modifiers;
    }

    return node;
}

ASTNode *create_short_node(short value)
{
    ASTNode *node = create_node(NODE_SHORT, VAR_SHORT, current_modifiers);
    SET_DATA_SHORT(node, value);
    return node;
}

ASTNode *create_float_node(float value)
{
    ASTNode *node = create_node(NODE_FLOAT, VAR_FLOAT, current_modifiers);
    SET_DATA_FLOAT(node, value);
    return node;
}

ASTNode *create_char_node(char value)
{
    ASTNode *node = create_node(NODE_CHAR, VAR_CHAR, current_modifiers);
    SET_DATA_INT(node, value); // Store char as integer
    return node;
}

ASTNode *create_boolean_node(bool value)
{
    ASTNode *node = create_node(NODE_BOOLEAN, VAR_BOOL, current_modifiers);
    SET_DATA_BOOL(node, value);
    return node;
}

ASTNode *create_identifier_node(const char *name)
{
    ASTNode *node = create_node(NODE_IDENTIFIER, NONE, current_modifiers);
    SET_DATA_NAME(node, name);
    return node;
}

ASTNode *create_assignment_node(const char *name, ASTNode *expr)
{
    ASTNode *node = create_node(NODE_ASSIGNMENT, current_var_type, get_current_modifiers());
    SET_DATA_OP(node, create_identifier_node(name), expr, OP_ASSIGN);
    return node;
}

ASTNode *create_declaration_node(const char *name, ASTNode *expr)
{
    ASTNode *node = create_node(NODE_DECLARATION, current_var_type, get_current_modifiers());
    SET_DATA_OP(node, create_identifier_node(name), expr, OP_ASSIGN);
    return node;
}

ASTNode *create_operation_node(OperatorType op, ASTNode *left, ASTNode *right)
{
    ASTNode *node = create_node(NODE_OPERATION, NONE, current_modifiers);
    SET_DATA_OP(node, left, right, op);
    return node;
}

ASTNode *create_unary_operation_node(OperatorType op, ASTNode *operand)
{
    ASTNode *node = create_node(NODE_UNARY_OPERATION, NONE, current_modifiers);
    SET_DATA_UNARY_OP(node, operand, op);
    return node;
}

ASTNode *create_for_statement_node(ASTNode *init, ASTNode *cond, ASTNode *incr, ASTNode *body)
{
    ASTNode *node = create_node(NODE_FOR_STATEMENT, NONE, current_modifiers);
    SET_DATA_FOR(node, init, cond, incr, body);
    return node;
}

ASTNode *create_while_statement_node(ASTNode *cond, ASTNode *body)
{
    ASTNode *node = create_node(NODE_WHILE_STATEMENT, NONE, current_modifiers);
    SET_DATA_WHILE(node, cond, body);
    return node;
}

ASTNode *create_do_while_statement_node(ASTNode *cond, ASTNode *body)
{
    ASTNode *node = create_node(NODE_DO_WHILE_STATEMENT, NONE, current_modifiers);
    SET_DATA_WHILE(node, cond, body);
    return node;
}

ASTNode *create_function_call_node(const char *func_name, ArgumentList *args)
{
    ASTNode *node = create_node(NODE_FUNC_CALL, NONE, current_modifiers);
    SET_DATA_FUNC_CALL(node, func_name, args);
    return node;
}

ASTNode *create_double_node(double value)
{
    ASTNode *node = create_node(NODE_DOUBLE, VAR_DOUBLE, current_modifiers);
    SET_DATA_DOUBLE(node, value);
    return node;
}

ASTNode *create_sizeof_node(ASTNode *expr)
{
    ASTNode *node = create_node(NODE_SIZEOF, NONE, current_modifiers);
    SET_SIZEOF(node, expr);
    return node;
}

// @param promotion: 0 for no promotion, 1 for promotion to double 2 for promotion to float
void *handle_identifier(ASTNode *node, const char *contextErrorMessage, int promote)
{
    if (!check_and_mark_identifier(node, contextErrorMessage)) {
        exit(1);
}

    const char *name = node->data.name;
    Variable *var = get_variable(name);
    if (var != NULL)
    {
        static Value promoted_value;
        if (promote == 1)
        {

            switch (var->var_type)
            {
            case VAR_DOUBLE:
                return &var->value.dvalue;
            case VAR_FLOAT:
                promoted_value.dvalue = (double)var->value.fvalue;
                return &promoted_value;
            case VAR_INT:
            case VAR_CHAR:
            case VAR_SHORT:
                promoted_value.dvalue = (double)var->value.svalue;
                return &promoted_value;
            case VAR_BOOL:
                promoted_value.dvalue = (double)var->value.ivalue;
                return &promoted_value;
            default:
                yyerror("Unsupported variable type");
                return NULL;
            }
        }
        else if (promote == 2)
        {
            switch (var->var_type)
            {
            case VAR_DOUBLE:
                promoted_value.fvalue = (float)var->value.dvalue;
                return &promoted_value.fvalue;
            case VAR_FLOAT:
                return &var->value.fvalue;
            case VAR_INT:
            case VAR_CHAR:
            case VAR_SHORT:
                promoted_value.fvalue = (float)var->value.svalue;
                break;
            case VAR_BOOL:
                promoted_value.fvalue = (float)var->value.ivalue;
                return &promoted_value.fvalue;
            default:
                yyerror("Unsupported variable type");
                return NULL;
            }
        }
        else
        {
            switch (var->var_type)
            {
            case VAR_DOUBLE:
                return &var->value.dvalue;
            case VAR_FLOAT:
                return &var->value.fvalue;
            case VAR_INT:
            case VAR_CHAR:
            case VAR_SHORT:
                return &var->value.svalue;
            case VAR_BOOL:
                return &var->value.ivalue;
            default:
                yyerror("Unsupported variable type");
                return NULL;
            }
        }
    }
    yyerror("Undefined variable");
    return NULL;
}

int get_expression_type(ASTNode *node)
{
    if (!node)
    {
        yyerror("Null node in get_expression_type");
        return NONE; // Return an unknown type if the node is null
    }

    switch (node->type)
    {
    case NODE_INT:
        return VAR_INT;
    case NODE_SHORT:
        return VAR_SHORT;
    case NODE_FLOAT:
        return VAR_FLOAT;
    case NODE_DOUBLE:
        return VAR_DOUBLE;
    case NODE_BOOLEAN:
        return VAR_BOOL;
    case NODE_CHAR:
        return VAR_INT;
    case NODE_ARRAY_ACCESS:
    {
        // First, get the array's base type from symbol table
        const char *array_name = node->data.array.name;
        Variable *var = get_variable(array_name);
        if (var != NULL)
        {
            // Found the array, now handle the index expression
            ASTNode *index_expr = node->data.array.index;

            // Recursively evaluate index expression type
            int index_type = get_expression_type(index_expr);
            if (index_type != VAR_INT && index_type != VAR_SHORT)
            {
                yyerror("Array index must be an integer type");
                return NONE;
            }

            // Return the array's element type
            return var->var_type;
        }
        yyerror("Undefined array in expression");
        return NONE;
    }
    case NODE_IDENTIFIER:
    {
        // Look up the variable type in the symbol table
        const char *array_name = node->data.name;
        Variable *var = get_variable(array_name);
        if (var != NULL)
        {
            return var->var_type;
        }
        yyerror("Undefined variable in get_expression_type");
        return NONE;
    }
    case NODE_OPERATION:
    {
        // For binary operations, evaluate both operands to determine the highest type
        int left_type = get_expression_type(node->data.op.left);
        int right_type = get_expression_type(node->data.op.right);

        // Promote to the highest type (int -> float -> double)
        if (left_type == VAR_DOUBLE || right_type == VAR_DOUBLE) {
            return VAR_DOUBLE;
        } if (left_type == VAR_FLOAT || right_type == VAR_FLOAT)
            return VAR_FLOAT;
        else
            return VAR_INT;
    }
    case NODE_UNARY_OPERATION:
    {
        return get_expression_type(node->data.unary.operand);
    }
    case NODE_SIZEOF:
    {
        return VAR_INT; // Sizeof always returns an integer
    }
    default:
        yyerror("Unknown node type in get_expression_type");
        return NONE;
    }
}

void *handle_binary_operation(ASTNode *node)
{
    if (!node || node->type != NODE_OPERATION)
    {
        yyerror("Invalid binary operation node");
        return NULL;
    }

    // Evaluate left and right operands.
    void *left_value = NULL;
    void *right_value = NULL;

    // Determine the actual types of the operands.
    int left_type = get_expression_type(node->data.op.left);
    int right_type = get_expression_type(node->data.op.right);

    // Promote types if necessary (short -> int -> float -> double).
    int promoted_type = VAR_SHORT;
    if (left_type == VAR_DOUBLE || right_type == VAR_DOUBLE) {
        promoted_type = VAR_DOUBLE;
    } else if (left_type == VAR_FLOAT || right_type == VAR_FLOAT) {
        promoted_type = VAR_FLOAT;
    } else if (left_type == VAR_INT || right_type == VAR_INT) {
        promoted_type = VAR_INT;
}

    // Allocate and evaluate operands based on promoted type.
    switch (promoted_type)
    {
    case VAR_INT:
        left_value = SAFE_MALLOC(int);
        right_value = SAFE_MALLOC(int);
        *(int *)left_value = evaluate_expression_int(node->data.op.left);
        *(int *)right_value = evaluate_expression_int(node->data.op.right);
        break;

    case VAR_FLOAT:
        left_value = SAFE_MALLOC(float);
        right_value = SAFE_MALLOC(float);
        *(float *)left_value = (left_type == VAR_INT)
                                   ? (float)evaluate_expression_int(node->data.op.left)
                                   : evaluate_expression_float(node->data.op.left);
        *(float *)right_value = (right_type == VAR_INT)
                                    ? (float)evaluate_expression_int(node->data.op.right)
                                    : evaluate_expression_float(node->data.op.right);
        break;

    case VAR_DOUBLE:
        left_value = SAFE_MALLOC(double);
        right_value = SAFE_MALLOC(double);
        *(double *)left_value = (left_type == VAR_INT)
                                    ? (double)evaluate_expression_int(node->data.op.left)
                                : (left_type == VAR_FLOAT)
                                    ? (double)evaluate_expression_float(node->data.op.left)
                                    : evaluate_expression_double(node->data.op.left);
        *(double *)right_value = (right_type == VAR_INT)
                                     ? (double)evaluate_expression_int(node->data.op.right)
                                 : (right_type == VAR_FLOAT)
                                     ? (double)evaluate_expression_float(node->data.op.right)
                                     : evaluate_expression_double(node->data.op.right);
        break;
    case VAR_SHORT:
        left_value = SAFE_MALLOC(short);
        right_value = SAFE_MALLOC(short);
        *(short *)left_value = evaluate_expression_short(node->data.op.left);
        *(short *)right_value = evaluate_expression_short(node->data.op.right);
        break;

    default:
        yyerror("Unsupported type promotion");
        return NULL;
    }

    // Perform the operation and allocate the result.
    void *result = NULL;
    if (promoted_type == VAR_DOUBLE)
    {
        result = SAFE_MALLOC(double);
    }
    else if (promoted_type == VAR_FLOAT)
    {
        result = SAFE_MALLOC(float);
    }
    else if (promoted_type == VAR_SHORT)
    {
        result = SAFE_MALLOC(short);
    }
    else
    {
        result = SAFE_MALLOC(int);
    }

    switch (node->data.op.op)
    {
    case OP_PLUS:
        if (promoted_type == VAR_INT) {
            *(int *)result = *(int *)left_value + *(int *)right_value;
        } else if (promoted_type == VAR_FLOAT) {
            *(float *)result = *(float *)left_value + *(float *)right_value;
        } else if (promoted_type == VAR_DOUBLE) {
            *(double *)result = *(double *)left_value + *(double *)right_value;
        } else if (promoted_type == VAR_SHORT) {
            *(short *)result = *(short *)left_value + *(short *)right_value;
}
        break;

    case OP_MINUS:
        if (promoted_type == VAR_INT) {
            *(int *)result = *(int *)left_value - *(int *)right_value;
        } else if (promoted_type == VAR_FLOAT) {
            *(float *)result = *(float *)left_value - *(float *)right_value;
        } else if (promoted_type == VAR_DOUBLE) {
            *(double *)result = *(double *)left_value - *(double *)right_value;
        } else if (promoted_type == VAR_SHORT) {
            *(short *)result = *(short *)left_value - *(short *)right_value;
}

        break;

    case OP_TIMES:
        if (promoted_type == VAR_INT) {
            *(int *)result = *(int *)left_value * *(int *)right_value;
        } else if (promoted_type == VAR_FLOAT) {
            *(float *)result = *(float *)left_value * *(float *)right_value;
        } else if (promoted_type == VAR_DOUBLE) {
            *(double *)result = *(double *)left_value * *(double *)right_value;
        } else if (promoted_type == VAR_SHORT) {
            *(short *)result = *(short *)left_value * *(short *)right_value;
}
        break;

    case OP_DIVIDE:
        if (promoted_type == VAR_INT)
        {
            if (*(int *)right_value == 0)
            {
                yyerror("Division by zero");
                *(int *)result = 0; // Define a fallback behavior for int division by zero
            }
            else
            {
                *(int *)result = *(int *)left_value / *(int *)right_value;
            }
        }
        else if (promoted_type == VAR_FLOAT)
        {
            float right = *(float *)right_value;
            float left = *(float *)left_value;

            if (fabsf(right) < __FLT_MIN__)
            {
                if (fabsf(left) < __FLT_MIN__)
                {
                    *(float *)result = -NAN; 
                }
                else
                {
                    *(float *)result = left > 0 ? __FLT_MAX__ : -__FLT_MAX__;
                }
            }
            else
            {
                *(float *)result = left / right;
            }
        }
        else if (promoted_type == VAR_DOUBLE)
        {
            double right = *(double *)right_value;
            double left = *(double *)left_value;

            if (fabs(right) < __DBL_MIN__)
            {
                if (fabs(left) < __DBL_MIN__)
                {
                    *(double *)result = -NAN;
                }
                else
                {
                    *(double *)result = left > 0 ? __DBL_MAX__ : -__DBL_MAX__;
                }
            }
            else
            {
                *(double *)result = left / right;
            }
        }
        else if (promoted_type == VAR_SHORT)
        {
            if (*(short *)right_value == 0)
            {
                yyerror("Division by zero");
                *(short *)result = 0; // Define a fallback behavior for short division by zero
            }
            else
            {
                *(short *)result = *(short *)left_value / *(short *)right_value;
            }
        }
        break;
    case OP_MOD:
        if (promoted_type == VAR_INT)
        {
            int left = *(int *)left_value;
            int right = *(int *)right_value;

            if (right == 0)
            {
                yyerror("Modulo by zero");
                *(int *)result = 0; // Define fallback for modulo by zero
            }
            else if (node->modifiers.is_unsigned)
            {
                // Explicitly handle unsigned modulo
                unsigned int ul = (unsigned int)left;
                unsigned int ur = (unsigned int)right;
                *(int *)result = (int)(ul % ur);
            }
            else
            {
                *(int *)result = left % right;
            }
        }
        else if (promoted_type == VAR_FLOAT)
        {
            *(float *)result = fmodf(*(float *)left_value, *(float *)right_value);
        }
        else if (promoted_type == VAR_DOUBLE)
        {
            *(double *)result = fmod(*(double *)left_value, *(double *)right_value);
        }
        else if (promoted_type == VAR_SHORT)
        {
            *(short *)result = *(short *)left_value % *(short *)right_value;
        }
        break;
    case OP_LT:
        if (promoted_type == VAR_INT) {
            *(int *)result = *(int *)left_value < *(int *)right_value;
        } else if (promoted_type == VAR_FLOAT) {
            *(float *)result = *(float *)left_value < *(float *)right_value;
        } else if (promoted_type == VAR_DOUBLE) {
            *(double *)result = *(double *)left_value < *(double *)right_value;
        } else if (promoted_type == VAR_SHORT) {
            *(short *)result = *(short *)left_value < *(short *)right_value;
}
        break;

    case OP_GT:
        if (promoted_type == VAR_INT) {
            *(int *)result = *(int *)left_value > *(int *)right_value;
        } else if (promoted_type == VAR_FLOAT) {
            *(float *)result = *(float *)left_value > *(float *)right_value;
        } else if (promoted_type == VAR_DOUBLE) {
            *(double *)result = *(double *)left_value > *(double *)right_value;
        } else if (promoted_type == VAR_SHORT) {
            *(short *)result = *(short *)left_value > *(short *)right_value;
}
        break;

    case OP_LE:
        if (promoted_type == VAR_INT) {
            *(int *)result = *(int *)left_value <= *(int *)right_value;
        } else if (promoted_type == VAR_FLOAT) {
            *(float *)result = *(float *)left_value <= *(float *)right_value;
        } else if (promoted_type == VAR_DOUBLE) {
            *(double *)result = *(double *)left_value <= *(double *)right_value;
        } else if (promoted_type == VAR_SHORT) {
            *(short *)result = *(short *)left_value <= *(short *)right_value;
}
        break;

    case OP_GE:
        if (promoted_type == VAR_INT) {
            *(int *)result = *(int *)left_value >= *(int *)right_value;
        } else if (promoted_type == VAR_FLOAT) {
            *(float *)result = *(float *)left_value >= *(float *)right_value;
        } else if (promoted_type == VAR_DOUBLE) {
            *(double *)result = *(double *)left_value >= *(double *)right_value;
        } else if (promoted_type == VAR_SHORT) {
            *(short *)result = *(short *)left_value >= *(short *)right_value;
}
        break;

    case OP_EQ:

        if (promoted_type == VAR_INT) {
            *(int *)result = *(int *)left_value == *(int *)right_value;
        } else if (promoted_type == VAR_FLOAT) {
            *(float *)result = *(float *)left_value == *(float *)right_value;
        } else if (promoted_type == VAR_DOUBLE) {
            *(double *)result = *(double *)left_value == *(double *)right_value;
        } else if (promoted_type == VAR_SHORT) {
            *(short *)result = *(short *)left_value == *(short *)right_value;
}
        break;

    case OP_NE:
        if (promoted_type == VAR_INT) {
            *(int *)result = *(int *)left_value != *(int *)right_value;
        } else if (promoted_type == VAR_FLOAT) {
            *(float *)result = *(float *)left_value != *(float *)right_value;
        } else if (promoted_type == VAR_DOUBLE) {
            *(double *)result = *(double *)left_value != *(double *)right_value;
        } else if (promoted_type == VAR_SHORT) {
            *(short *)result = *(short *)left_value != *(short *)right_value;
}
        break;

    default:
        yyerror("Unsupported binary operator");
        SAFE_FREE(result);
        result = NULL;
    }

    SAFE_FREE(left_value);
    SAFE_FREE(right_value);

    return result;
}

void *handle_unary_expression(const ASTNode *node, void *operand_value, int operand_type)
{
    switch (node->data.unary.op)
    {
    case OP_NEG:
        if (operand_type == VAR_INT)
        {
            int *result = SAFE_MALLOC(int);
            *result = -(*(int *)operand_value);
            return result;
        }
        else if (operand_type == VAR_SHORT)
        {
            short *result = SAFE_MALLOC(short);
            *result = !(*(short *)operand_value);
            return result;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float *result = SAFE_MALLOC(float);
            *result = -(*(float *)operand_value);
            return result;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double *result = SAFE_MALLOC(double);
            *result = -(*(double *)operand_value);
            return result;
        }
        else if (operand_type == VAR_BOOL)
        {
            bool *result = SAFE_MALLOC(bool);
            *result = !(*(bool *)operand_value);
            return result;
        }
        else
        {
            yyerror("Invalid type for unary negation");
            return NULL;
        }

    case OP_PRE_INC:
        if (operand_type == VAR_INT)
        {
            int *result = SAFE_MALLOC(int);
            *result = *(int *)operand_value + 1;
            set_int_variable(node->data.unary.operand->data.name, *result, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_SHORT)
        {
            short *result = SAFE_MALLOC(short);
            *result = *(short *)operand_value + 1;
            set_short_variable(node->data.unary.operand->data.name, *result, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float *result = SAFE_MALLOC(float);
            *result = *(float *)operand_value + 1;
            set_float_variable(node->data.unary.operand->data.name, *result, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double *result = SAFE_MALLOC(double);
            *result = *(double *)operand_value + 1;
            set_double_variable(node->data.unary.operand->data.name, *result, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else
        {
            yyerror("Invalid type for pre-increment");
            return NULL;
        }
    case OP_PRE_DEC:
        if (operand_type == VAR_INT)
        {
            int *result = SAFE_MALLOC(int);
            *result = *(int *)operand_value - 1;
            set_int_variable(node->data.unary.operand->data.name, *result, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_SHORT)
        {
            short *result = SAFE_MALLOC(short);
            *result = *(short *)operand_value - 1;
            set_short_variable(node->data.unary.operand->data.name, *result, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float *result = SAFE_MALLOC(float);
            *result = *(float *)operand_value - 1;
            set_float_variable(node->data.unary.operand->data.name, *result, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double *result = SAFE_MALLOC(double);
            *result = *(double *)operand_value - 1;
            set_double_variable(node->data.unary.operand->data.name, *result, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else
        {
            yyerror("Invalid type for pre-decrement");
            return NULL;
        }
    case OP_POST_INC:
        if (operand_type == VAR_INT)
        {
            int *result = SAFE_MALLOC(int);
            *result = *(int *)operand_value;
            set_int_variable(node->data.unary.operand->data.name, *result + 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_SHORT)
        {
            short *result = SAFE_MALLOC(short);
            *result = *(short *)operand_value;
            set_short_variable(node->data.unary.operand->data.name, *result + 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float *result = SAFE_MALLOC(float);
            *result = *(float *)operand_value;
            set_float_variable(node->data.unary.operand->data.name, *result + 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double *result = SAFE_MALLOC(double);
            *result = *(double *)operand_value;
            set_double_variable(node->data.unary.operand->data.name, *result + 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else
        {
            yyerror("Invalid type for post-increment");
            return NULL;
        }
    case OP_POST_DEC:
        if (operand_type == VAR_INT)
        {
            int *result = SAFE_MALLOC(int);
            *result = *(int *)operand_value;
            set_int_variable(node->data.unary.operand->data.name, *result - 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_SHORT)
        {
            short *result = SAFE_MALLOC(short);
            *result = *(short *)operand_value;
            set_short_variable(node->data.unary.operand->data.name, *result - 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float *result = SAFE_MALLOC(float);
            *result = *(float *)operand_value;
            set_float_variable(node->data.unary.operand->data.name, *result - 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double *result = SAFE_MALLOC(double);
            *result = *(double *)operand_value;
            set_double_variable(node->data.unary.operand->data.name, *result - 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else
        {
            yyerror("Invalid type for post-decrement");
            return NULL;
        }
    default:
        yyerror("Unknown unary operator");
        return NULL;
    }
}

float evaluate_expression_float(ASTNode *node)
{
    if (!node) {
        return 0.0F;
}

    switch (node->type)
    {
    case NODE_ARRAY_ACCESS:
    {
        const char *array_name = node->data.array.name;
        int idx = evaluate_expression_int(node->data.array.index);
        Variable *var = get_variable(array_name);
        if (var != NULL)
        {
            if (!var->is_array)
            {
                yyerror("Not an array!");
                return 0.0F;
            }
            if (idx < 0 || idx >= var->array_length)
            {
                yyerror("Array index out of bounds!");
                return 0.0F;
            }

            // Return the value based on the array's actual type
            switch (var->var_type)
            {
            case VAR_FLOAT:
                return var->value.farray[idx];
            case VAR_DOUBLE:
                return (float)var->value.darray[idx];
            case VAR_INT:
                return (float)var->value.iarray[idx];
            case VAR_SHORT:
                return (float)var->value.sarray[idx];
            case VAR_BOOL:
                return (float)var->value.barray[idx];
            case VAR_CHAR:
                return (float)var->value.carray[idx];
            default:
                yyerror("Unsupported array type");
                return 0.0F;
            }
        }
        yyerror("Undefined array variable!");
        return 0.0F;
    }
    case NODE_FLOAT:
        return node->data.fvalue;
    case NODE_DOUBLE:
        return (float)node->data.dvalue;
    case NODE_INT:
        return (float)node->data.ivalue;
    case NODE_IDENTIFIER:
    {
        return *(float *)handle_identifier(node, "Undefined variable", 2);
    }
    case NODE_OPERATION:
    {
        int result_type = get_expression_type(node);
        void *result = handle_binary_operation(node);
        float result_float = 0.0F;
        result_float = (result_type == VAR_INT)
                           ? (float)(*(int *)result)
                       : (result_type == VAR_FLOAT)
                           ? *(float *)result
                           : (float)(*(double *)result);
        SAFE_FREE(result);
        return result_float;
    }
    case NODE_UNARY_OPERATION:
    {
        float operand = evaluate_expression_float(node->data.unary.operand);
        float *result = (float *)handle_unary_expression(node, &operand, VAR_FLOAT);
        float return_val = *result;
        SAFE_FREE(result);
        return return_val;
    }
    case NODE_SIZEOF:
    {
        return (float)handle_sizeof(node);
    }
    case NODE_FUNC_CALL:
    {
        float *res = (float *)handle_function_call(node);
        if (res != NULL)
        {
            float result = *res;
            SAFE_FREE(res);
            return result;
        }
        return 0.0F;
    }
    default:
        yyerror("Invalid float expression");
        return 0.0F;
    }
}

double evaluate_expression_double(ASTNode *node)
{
    if (!node) {
        return 0.0L;
}

    switch (node->type)
    {
    case NODE_ARRAY_ACCESS:
    {
        const char *array_name = node->data.array.name;
        int idx = evaluate_expression_int(node->data.array.index);

        Variable *var = get_variable(array_name);
        if (var != NULL)
        {
            if (!var->is_array)
            {
                yyerror("Not an array!");
                return 0.0L;
            }
            if (idx < 0 || idx >= var->array_length)
            {
                yyerror("Array index out of bounds!");
                return 0.0L;
            }

            // Return the value based on the array's actual type
            switch (var->var_type)
            {
            case VAR_FLOAT:
                return (double)var->value.farray[idx];
            case VAR_DOUBLE:
                return var->value.darray[idx];
            case VAR_INT:
                return (double)var->value.iarray[idx];
            case VAR_SHORT:
                return (double)var->value.sarray[idx];
            case VAR_BOOL:
                return (double)var->value.barray[idx];
            case VAR_CHAR:
                return (double)var->value.carray[idx];
            default:
                yyerror("Unsupported array type");
                return 0.0L;
            }
        }
        yyerror("Undefined array variable!");
        return 0.0L;
    }
    case NODE_DOUBLE:
        return node->data.dvalue;
    case NODE_FLOAT:
        return (double)node->data.fvalue;
    case NODE_INT:
        return (double)node->data.ivalue;
    case NODE_IDENTIFIER:
    {
        return *(double *)handle_identifier(node, "Undefined variable", 1);
    }
    case NODE_OPERATION:
    {
        int result_type = get_expression_type(node);
        void *result = handle_binary_operation(node);
        double result_double = 0.0L;
        result_double = (result_type == VAR_INT)
                            ? (double)(*(int *)result)
                        : (result_type == VAR_FLOAT)
                            ? (double)(*(float *)result)
                            : *(double *)result;
        SAFE_FREE(result);
        return result_double;
    }
    case NODE_UNARY_OPERATION:
    {
        double operand = evaluate_expression_double(node->data.unary.operand);
        double *result = (double *)handle_unary_expression(node, &operand, VAR_DOUBLE);
        double return_val = *result;
        SAFE_FREE(result);
        return return_val;
    }
    case NODE_SIZEOF:
    {
        return (double)handle_sizeof(node);
    }
    case NODE_FUNC_CALL:
    {
        double *res = (double *)handle_function_call(node);
        if (res != NULL)
        {
            double result = *res;
            SAFE_FREE(res);
            return result;
        }
        return 0.0L;
    }
    default:
        yyerror("Invalid double expression");
        return 0.0L;
    }
}
size_t get_type_size(const char *name)
{
    const Variable *var = get_variable(name);
    if (var != NULL)
    {
        if (var->var_type == VAR_FLOAT)
        {
            if (var->is_array)
            {
                return sizeof(float) * var->array_length;
            }
            return sizeof(float);
        }
        if (var->var_type == VAR_DOUBLE)
        {
            if (var->is_array)
            {
                return sizeof(double) * var->array_length;
            }
            return sizeof(double);
        }
        else if (var->modifiers.is_unsigned && var->var_type == VAR_INT)
        {
            if (var->is_array)
            {
                return sizeof(unsigned int) * var->array_length;
            }
            return sizeof(unsigned int);
        }
        else if (var->var_type == VAR_BOOL)
        {
            if (var->is_array)
            {
                return sizeof(bool) * var->array_length;
            }
            return sizeof(bool);
        }
        else if (var->modifiers.is_unsigned && var->var_type == VAR_SHORT)
        {
            if (var->is_array)
            {
                return sizeof(unsigned short) * var->array_length;
            }
            return sizeof(unsigned short);
        }
        else if (var->var_type == VAR_SHORT)
        {
            if (var->is_array)
            {
                return sizeof(short) * var->array_length;
            }
            return sizeof(short);
        }
        else if (var->var_type == VAR_INT)
        {
            if (var->is_array)
            {
                return sizeof(int) * var->array_length;
            }
            return sizeof(int);
        }
        else
        {
            yyerror("Undefined variable in sizeof");
        }
    }
    yyerror("Undefined variable in sizeof");
    return 0;
}

size_t handle_sizeof(ASTNode *node)
{
    const ASTNode *expr = node->data.sizeof_stmt.expr;
    VarType type = get_expression_type(node->data.sizeof_stmt.expr);
    if (expr->type == NODE_IDENTIFIER)
    {
        return get_type_size(expr->data.name);
    }
    switch (type)
    {
    case VAR_INT:
        return sizeof(int);
    case VAR_FLOAT:
        return sizeof(float);
    case VAR_DOUBLE:
        return sizeof(double);
    case VAR_SHORT:
        return sizeof(short);
    case VAR_BOOL:
        return sizeof(bool);
    case VAR_CHAR:
        return sizeof(char);
    default:
        yyerror("Invalid type in sizeof");
        return 0;
    }
    yyerror("Invalid type in sizeof");
    return 0;
}

short evaluate_expression_short(ASTNode *node)
{
    if (!node) {
        return 0;
}

    switch (node->type)
    {
    case NODE_INT:
        return (short)node->data.ivalue;
    case NODE_BOOLEAN:
        return (short)node->data.bvalue; // Already 1 or 0
    case NODE_CHAR:                      // Add explicit handling for characters
        return (short)node->data.ivalue;
    case NODE_SHORT:
        return node->data.svalue;
    case NODE_FLOAT:
        yyerror("Cannot use float in integer context");
        return (short)node->data.fvalue;
    case NODE_DOUBLE:
        yyerror("Cannot use double in integer context");
        return (short)node->data.dvalue;
    case NODE_SIZEOF:
    {
        return handle_sizeof(node);
    }
    case NODE_IDENTIFIER:
    {
        return *(short *)handle_identifier(node, "Undefined variable", 0);
    }
    case NODE_OPERATION:
    {
        // Special handling for logical operations
        if (node->data.op.op == OP_AND || node->data.op.op == OP_OR)
        {
            short left = evaluate_expression_short(node->data.op.left);
            short right = evaluate_expression_short(node->data.op.right);

            switch (node->data.op.op)
            {
            case OP_AND:
                return left && right;
            case OP_OR:
                return left || right;
            default:
                break;
            }
        }

        // Regular integer operations
        int result_type = get_expression_type(node);
        void *result = handle_binary_operation(node );
        short result_short = 0;
        result_short = (result_type == VAR_SHORT)
                           ? *(short *)result
                       : (result_type == VAR_FLOAT)
                           ? (short)(*(float *)result)
                       : (result_type == VAR_DOUBLE)
                           ? (short)(*(double *)result)
                           : (short)(*(int *)result);
        SAFE_FREE(result);
        return result_short;
    }
    case NODE_UNARY_OPERATION:
    {
        short operand = evaluate_expression_short(node->data.unary.operand);
        short *result = (short *)handle_unary_expression(node, &operand, VAR_SHORT);
        short return_val = *result;
        SAFE_FREE(result);
        return return_val;
    }
    case NODE_ARRAY_ACCESS:
    {
        // find the symbol
        const char *name = node->data.array.name;
        Variable *var = get_variable(name);
        if (var != NULL)
        {
            if (!var->is_array)
            {
                yyerror("Not an array!");
                return 0;
            }
            // Evaluate index
            int idx = evaluate_expression_int(node->data.array.index);
            if (idx < 0 || idx >= var->array_length)
            {
                yyerror("Array index out of bounds!");
                return 0;
            }
            switch (node->var_type)
            {
            case VAR_INT:
                return (short)var->value.iarray[idx];
            case VAR_SHORT:
                return var->value.sarray[idx];
            case VAR_FLOAT:
                return (short)var->value.farray[idx];
            case VAR_DOUBLE:
                return (short)var->value.darray[idx];
            case VAR_BOOL:
                return (short)var->value.barray[idx];
            case VAR_CHAR:
                return (short)var->value.carray[idx];
            default:
                yyerror("Undefined array type!");
            }
        }
        yyerror("Undefined array variable!");
        return 0;
    }
    case NODE_FUNC_CALL:
    {
        short *res = (short *)handle_function_call(node);
        if (res != NULL)
        {
            short return_val = *res;
            SAFE_FREE(res);
            return return_val;
        }
        return 0;
    }
    default:
        yyerror("Invalid short expression");
        return 0;
    }
}

int evaluate_expression_int(ASTNode *node)
{
    if (!node) {
        return 0;
}

    switch (node->type)
    {
    case NODE_INT:
        return node->data.ivalue;
    case NODE_BOOLEAN:
        return node->data.bvalue; // Already 1 or 0
    case NODE_CHAR:               // Add explicit handling for characters
        return node->data.ivalue;
    case NODE_SHORT:
        return node->data.svalue;
    case NODE_FLOAT:
        yyerror("Cannot use float in integer context");
        return (int)node->data.fvalue;
    case NODE_DOUBLE:
        yyerror("Cannot use double in integer context");
        return (int)node->data.dvalue;
    case NODE_SIZEOF:
    {
        return handle_sizeof(node);
    }
    case NODE_IDENTIFIER:
    {
        return *(int *)handle_identifier(node, "Undefined variable", 0);
    }
    case NODE_OPERATION:
    {
        // Special handling for logical operations
        if (node->data.op.op == OP_AND || node->data.op.op == OP_OR)
        {
            int left = evaluate_expression_int(node->data.op.left);
            int right = evaluate_expression_int(node->data.op.right);

            switch (node->data.op.op)
            {
            case OP_AND:
                return left && right;
            case OP_OR:
                return left || right;
            default:
                break;
            }
        }

        // Regular integer operations
        int result_type = get_expression_type(node);
        void *result = handle_binary_operation(node);
        int result_int = 0;
        result_int = (result_type == VAR_INT)
                         ? *(int *)result
                     : (result_type == VAR_FLOAT)
                         ? (int)(*(float *)result)
                         : (int)(*(double *)result);
        SAFE_FREE(result);
        return result_int;
    }
    case NODE_UNARY_OPERATION:
    {
        int operand = evaluate_expression_int(node->data.unary.operand);
        int *result = (int *)handle_unary_expression(node, &operand, VAR_INT);
        int return_val = *result;
        SAFE_FREE(result);
        return return_val;
    }
    case NODE_ARRAY_ACCESS:
    {
        // find the symbol
        const char *name = node->data.array.name;
        Variable *var = get_variable(name);
        if (var != NULL)
        {
            if (!var->is_array)
            {
                yyerror("Not an array!");
                return 0;
            }
            // Evaluate index
            int idx = evaluate_expression_int(node->data.array.index);
            if (idx < 0 || idx >= var->array_length)
            {
                yyerror("Array index out of bounds!");
                return 0;
            }
            switch (node->var_type)
            {
            case VAR_INT:
                return var->value.iarray[idx];
            case VAR_SHORT:
                return var->value.sarray[idx];
            case VAR_FLOAT:
                return var->value.farray[idx];
            case VAR_DOUBLE:
                return var->value.darray[idx];
            case VAR_BOOL:
                return var->value.barray[idx];
            case VAR_CHAR:
                return var->value.carray[idx];
            default:
                yyerror("Undefined array type!");
            }
        }
        yyerror("Undefined array variable!");
        return 0;
    }
    case NODE_FUNC_CALL:
    {
        int *res = (int *)handle_function_call(node);
        if (res != NULL)
        {
            int return_val = *res;
            SAFE_FREE(res);
            return return_val;
        }
        return 0;
    }
    default:
        yyerror("Invalid integer expression");
        return 0;
    }
}

void *handle_function_call(ASTNode *node)
{
    execute_function_call(
        node->data.func_call.function_name,
        node->data.func_call.arguments);
    void *return_value = NULL;
    if (current_return_value.has_value)
    {
        switch (current_return_value.type)
        {
        case VAR_INT:
            return_value = SAFE_MALLOC(int);
            *(int *)return_value = current_return_value.value.ivalue;
            break;
        case VAR_FLOAT:
            return_value = SAFE_MALLOC(float);
            *(float *)return_value = current_return_value.value.fvalue;
            break;
        case VAR_DOUBLE:
            return_value = SAFE_MALLOC(double);
            *(double *)return_value = current_return_value.value.dvalue;
            break;
        case VAR_BOOL:
            return_value = SAFE_MALLOC(bool);
            *(bool *)return_value = current_return_value.value.bvalue;
            break;
        case VAR_CHAR:
            return_value = SAFE_MALLOC(char);
            *(char *)return_value = current_return_value.value.ivalue;
            break;
        case VAR_SHORT:
            return_value = SAFE_MALLOC(short);
            *(short *)return_value = current_return_value.value.svalue;
            break;
        case NONE:
            return NULL;
        }
    }
    return return_value;
}

bool evaluate_expression_bool(ASTNode *node)
{
    if (!node) {
        return 0;
}

    switch (node->type)
    {
    case NODE_INT:
        return (bool)node->data.ivalue;
    case NODE_SHORT:
        return (bool)node->data.svalue;
    case NODE_BOOLEAN:
        return node->data.bvalue;
    case NODE_CHAR:
        return (bool)node->data.ivalue;
    case NODE_FLOAT:
        return (bool)node->data.fvalue;
    case NODE_DOUBLE:
        return (bool)node->data.dvalue;
    case NODE_IDENTIFIER:
    {
        return *(bool *)handle_identifier(node, "Undefined variable", 0);
    }
    case NODE_OPERATION:
    {
        // Special handling for logical operations
        if (node->data.op.op == OP_AND || node->data.op.op == OP_OR)
        {
            bool left = evaluate_expression_bool(node->data.op.left);
            bool right = evaluate_expression_bool(node->data.op.right);

            switch (node->data.op.op)
            {
            case OP_AND:
                return left && right;
            case OP_OR:
                return left || right;
            default:
                break;
            }
        }

        // Regular integer operations
        int result_type = get_expression_type(node);
        void *result = handle_binary_operation(node);
        bool result_bool = 0;
        result_bool = (result_type == VAR_INT)
                          ? (bool)(*(int *)result)
                      : (result_type == VAR_FLOAT)
                          ? (bool)(*(float *)result)
                          : (bool)(*(double *)result);
        SAFE_FREE(result);
        return result_bool;
    }
    case NODE_UNARY_OPERATION:
    {
        bool operand = evaluate_expression_bool(node->data.unary.operand);
        bool *result = (bool *)handle_unary_expression(node, &operand, VAR_BOOL);
        bool return_val = *result;
        SAFE_FREE(result);
        return return_val;
    }
    case NODE_ARRAY_ACCESS:
    {
        // find the symbol
        const char *name = node->data.array.name;
        Variable *var = get_variable(name);
        if (var != NULL)
        {
            if (!var->is_array)
            {
                yyerror("Not an array!");
                return 0;
            }
            // Evaluate index
            int idx = evaluate_expression_int(node->data.array.index);
            if (idx < 0 || idx >= var->array_length)
            {
                yyerror("Array index out of bounds!");
                return 0;
            }
            switch (node->var_type)
            {
            case VAR_INT:
                return (bool)var->value.iarray[idx];
            case VAR_SHORT:
                return (bool)var->value.sarray[idx];
            case VAR_FLOAT:
                return (bool)var->value.farray[idx];
            case VAR_DOUBLE:
                return (bool)var->value.darray[idx];
            case VAR_BOOL:
                return var->value.barray[idx];
            case VAR_CHAR:
                return (bool)var->value.carray[idx];
            default:
                yyerror("Undefined array type!");
            }
        }
        yyerror("Undefined array variable!");
        return 0;
    }
    case NODE_FUNC_CALL:
    {
        bool *res = (bool *)handle_function_call(node);
        if (res != NULL)
        {
            bool return_val = *res;
            SAFE_FREE(res);
            return return_val;
        }
        return 0;
    }
    default:
        yyerror("Invalid boolean expression");
        return 0;
    }
}

ArgumentList *create_argument_list(ASTNode *expr, ArgumentList *existing_list)
{
    ArgumentList *new_node = SAFE_MALLOC(ArgumentList);
    new_node->expr = expr;
    new_node->next = NULL;

    if (!existing_list)
    {
        return new_node;
    }
    
            /* Append to the end of existing_list */
        ArgumentList *temp = existing_list;
        while (temp->next)
        {
            temp = temp->next;
        }
        temp->next = new_node;
        return existing_list;
   
}

ASTNode *create_print_statement_node(ASTNode *expr)
{
    ASTNode *node = SAFE_MALLOC(ASTNode);
    node->type = NODE_PRINT_STATEMENT;
    node->data.op.left = expr;
    return node;
}

ASTNode *create_error_statement_node(ASTNode *expr)
{
    ASTNode *node = SAFE_MALLOC(ASTNode);
    node->type = NODE_ERROR_STATEMENT;
    node->data.op.left = expr;
    return node;
}

ASTNode *create_statement_list(ASTNode *statement, ASTNode *existing_list)
{
    if (!existing_list)
    {
        // If there's no existing list, create a new one
        ASTNode *node = SAFE_MALLOC(ASTNode);
        if (!node)
        {
            yyerror("Memory allocation failed");
            return NULL;
        }
        node->type = NODE_STATEMENT_LIST;
        node->data.statements = SAFE_MALLOC(StatementList);
        if (!node->data.statements)
        {
            SAFE_FREE(node);
            yyerror("Memory allocation failed");
            return NULL;
        }
        node->data.statements->statement = statement;
        node->data.statements->next = NULL;
        return node;
    }
    
            // Append at the end of existing_list
        StatementList *sl = existing_list->data.statements;
        while (sl->next)
        {
            sl = sl->next;
        }
        // Now sl is the last element; append the new statement
        StatementList *new_item = SAFE_MALLOC(StatementList);
        if (!new_item)
        {
            yyerror("Memory allocation failed");
            return existing_list;
        }
        new_item->statement = statement;
        new_item->next = NULL;
        sl->next = new_item;
        return existing_list;
   
}

bool is_const_variable(const char *name)
{
    Variable *var = get_variable(name);
    if (var != NULL)
    {
        return var->modifiers.is_const;
    }
    return false;
}

void check_const_assignment(const char *name)
{
    if (is_const_variable(name))
    {
        yylineno = yylineno - 2;
        yyerror("Cannot modify const variable");
        exit(EXIT_FAILURE);
    }
}

bool is_short_expression(ASTNode *node)
{
    if (!node) {
        return false;
}

    switch (node->type)
    {
    case NODE_SHORT:
        return true;
    case NODE_FLOAT:
        return false;
    case NODE_INT:
        return false;
    case NODE_DOUBLE:
        return false;
    case NODE_IDENTIFIER:
    {
        if (!check_and_mark_identifier(node, "Undefined variable in type check")) {
            exit(1);
}
        const Variable *var = get_variable(node->data.name);
        if (var != NULL)
        {
            return var->var_type == VAR_SHORT;
        }
        yyerror("Undefined variable in type check");
        return false;
    }
    case NODE_OPERATION:
    {
        // If either operand is short, result is short
        return is_short_expression(node->data.op.left) ||
               is_short_expression(node->data.op.right);
    }
    case NODE_FUNC_CALL:
    {
        return get_function_return_type(node->data.func_call.function_name) == VAR_SHORT;
    }
    default:
        return false;
    }
}

Function *get_function(const char *name)
{
    Function *func = function_table;
    while (func != NULL)
    {
        if (strcmp(func->name, name) == 0)
        {
            return func;
        }
        func = func->next;
    }
    return NULL;
}

VarType get_function_return_type(const char *name)
{
    Function *func = get_function(name);
    if (func != NULL)
    {
        return func->return_type;
    }
    yyerror("Undefined function in type check");
    return NONE;
}

bool is_float_expression(ASTNode *node)
{
    if (!node) {
        return false;
}

    switch (node->type)
    {
    case NODE_FLOAT:
        return true;
    case NODE_INT:
        return false;
    case NODE_DOUBLE:
        return false;
    case NODE_IDENTIFIER:
    {
        if (!check_and_mark_identifier(node, "Undefined variable in type check")) {
            exit(1);
}
        const Variable *var = get_variable(node->data.name);
        if (var != NULL)
        {
            return var->var_type == VAR_FLOAT;
        }
        yyerror("Undefined variable in type check");
        return false;
    }
    case NODE_OPERATION:
    {
        // If either operand is float, result is float
        return is_float_expression(node->data.op.left) ||
               is_float_expression(node->data.op.right);
    }
    case NODE_FUNC_CALL:
    {
        return get_function_return_type(node->data.func_call.function_name) == VAR_FLOAT;
    }
    default:
        return false;
    }
}

bool is_double_expression(ASTNode *node)
{
    if (!node) {
        return false;
}

    switch (node->type)
    {
    case NODE_DOUBLE:
        return true;
    case NODE_FLOAT:
        return false;
    case NODE_INT:
        return false;
    case NODE_IDENTIFIER:
    {
        if (!check_and_mark_identifier(node, "Undefined variable in type check")) {
            exit(1);
}
        const Variable *var = get_variable(node->data.name);
        if (var != NULL)
        {
            return var->var_type == VAR_DOUBLE;
        }
        yyerror("Undefined variable in type check");
        return false;
    }
    case NODE_OPERATION:
    {
        // If either operand is double, result is double
        return is_double_expression(node->data.op.left) ||
               is_double_expression(node->data.op.right);
    }
    case NODE_FUNC_CALL:
    {
        return get_function_return_type(node->data.func_call.function_name) == VAR_DOUBLE;
    }
    default:
        return false;
    }
}

int evaluate_expression(ASTNode *node)
{
    if (is_short_expression(node))
    {
        return evaluate_expression_short(node);
    }
    if (is_float_expression(node))
    {
        return (int)evaluate_expression_float(node);
    }
    if (is_double_expression(node))
    {
        return (int)evaluate_expression_double(node);
    }
    return evaluate_expression_int(node);
}

void execute_assignment(ASTNode *node)
{
    if (node->type != NODE_ASSIGNMENT)
    {
        yyerror("Expected assignment node");
        return;
    }

    const char *name = node->data.op.left->data.name;
    check_const_assignment(name);

    ASTNode *value_node = node->data.op.right;
    TypeModifiers mods = node->modifiers;

    if (node->data.op.left->type == NODE_ARRAY_ACCESS)
    {
        // Evaluate the right side with proper type handling
        const char *array_name = node->data.op.left->data.array.name;
        int idx = evaluate_expression_int(node->data.op.left->data.array.index);

        // Find array in symbol table
        Variable *var = get_variable(array_name);
        if (var != NULL)
        {
            if (!var->is_array)
            {
                yyerror("Not an array!");
                return;
            }
            if (idx < 0 || idx >= var->array_length)
            {
                yyerror("Array index out of bounds!");
                return;
            }

            // Use the array's actual type for assignment
            switch (var->var_type)
            {
            case VAR_FLOAT:
                var->value.farray[idx] = evaluate_expression_float(node->data.op.right);
                break;
            case VAR_DOUBLE:
                var->value.darray[idx] = evaluate_expression_double(node->data.op.right);
                break;
            case VAR_INT:
                var->value.iarray[idx] = evaluate_expression_int(node->data.op.right);
                break;
            case VAR_SHORT:
                var->value.sarray[idx] = evaluate_expression_short(node->data.op.right);
                break;
            default:
                yyerror("Unsupported array type");
                free_ast(node);
                return;
            }
            free_ast(node);
            return;
        }
        yyerror("Undefined array variable");
        free_ast(node);
        return;
    }

    // Handle type conversion for float to int
    if (value_node->type == NODE_FLOAT || is_float_expression(value_node))
    {
        float value = evaluate_expression_float(value_node);
        if (node->data.op.left->type == NODE_INT)
        {
            // Check for overflow
            if (value > INT_MAX || value < INT_MIN)
            {
                yyerror("Float to int conversion overflow");
                value = INT_MAX;
            }
            if (!set_int_variable(name, (int)value, mods))
            {
                yyerror("Failed to set integer variable");
            }
            free_ast(node);
            return;
        }
    }

    if (is_float_expression(value_node))
    {
        float value = evaluate_expression_float(value_node);
        if (!set_float_variable(name, value, mods))
        {
            yyerror("Failed to set float variable");
        }
    }
    else if (is_double_expression(value_node))
    {
        double value = evaluate_expression_double(value_node);
        if (!set_double_variable(name, value, mods))
        {
            yyerror("Failed to set double variable");
        }
    }
    else if (is_short_expression(value_node))
    {
        short value = evaluate_expression_short(value_node);
        if (!set_short_variable(name, value, mods))
        {
            yyerror("Failed to set short variable");
        }
    }
    else
    {
        int value = evaluate_expression_int(value_node);
        if (!set_int_variable(name, value, mods))
        {
            yyerror("Failed to set integer variable");
        }
    }
}

void execute_statement(ASTNode *node)
{
    if (!node) {
        return;
}
    char *name = NULL;
    Variable *var = NULL;
    switch (node->type)
    {
    case NODE_DECLARATION:
        name = node->data.op.left->data.name;
        var = variable_new(name);
        add_variable_to_scope(name, var);
        SAFE_FREE(var);
    case NODE_ASSIGNMENT:
    {
        name = node->data.op.left->data.name;
        check_const_assignment(name);

        // Handle array assignment
        if (node->data.op.left->type == NODE_ARRAY_ACCESS)
        {
            ASTNode *array_node = node->data.op.left;
            const char *array_name = array_node->data.array.name;
            int idx = evaluate_expression_int(array_node->data.array.index);

            // Find array in symbol table
            var = get_variable(array_name);
            if (var != NULL)
            {
                if (!var->is_array)
                {
                    yyerror("Not an array!");
                    return;
                }
                if (idx < 0 || idx >= var->array_length)
                {
                    yyerror("Array index out of bounds!");
                    return;
                }

                switch (var->var_type)
                {
                case VAR_FLOAT:
                    var->value.farray[idx] = evaluate_expression_float(node->data.op.right);
                    break;
                case VAR_DOUBLE:
                    var->value.darray[idx] = evaluate_expression_double(node->data.op.right);
                    break;
                case VAR_INT:
                    var->value.iarray[idx] = evaluate_expression_int(node->data.op.right);
                    break;
                case VAR_SHORT:
                    var->value.sarray[idx] = evaluate_expression_short(node->data.op.right);
                    break;
                case VAR_BOOL:
                    var->value.barray[idx] = evaluate_expression_bool(node->data.op.right);
                    break;
                case VAR_CHAR:
                    var->value.carray[idx] = (char)evaluate_expression_int(node->data.op.right);
                    break;
                default:
                    yyerror("Unsupported array type");
                    return;
                }
                return;
            }
            yyerror("Undefined array variable");
            return;
        }

        ASTNode *value_node = node->data.op.right;
        TypeModifiers mods = node->modifiers;

        if (value_node->type == NODE_CHAR)
        {
            // Handle character assignments directly
            if (!set_int_variable(name, value_node->data.ivalue, mods))
            {
                yyerror("Failed to set character variable");
            }
        }
        else if (value_node->type == NODE_BOOLEAN)
        {
            if (!set_bool_variable(name, value_node->data.bvalue, mods))
            {
                yyerror("Failed to set boolean variable");
            }
        }
        else if (value_node->type == NODE_SHORT)
        {
            if (!set_short_variable(name, value_node->data.svalue, mods))
            {
                yyerror("Failed to set short variable");
            }
        }
        else if (node->var_type == VAR_FLOAT || is_float_expression(value_node))
        {
            float value = evaluate_expression_float(value_node);
            if (!set_float_variable(name, value, mods))
            {
                yyerror("Failed to set float variable");
            }
        }
        else if (node->var_type == VAR_DOUBLE || is_double_expression(value_node))
        {
            double value = evaluate_expression_double(value_node);
            if (!set_double_variable(name, value, mods))
            {
                yyerror("Failed to set double variable");
            }
        }
        else
        {
            int value = evaluate_expression_int(value_node);
            if (!set_int_variable(name, value, mods))
            {
                yyerror("Failed to set integer variable");
            }
        }
        break;
    }
    case NODE_ARRAY_ACCESS:
        if (node->data.array.name && node->data.array.index)
        {
            int length = node->data.array.index->data.ivalue;
            if (!(node->data.array.name && length && node->var_type))
            {
                yyerror("Failed to create array");
            }
        }
        break;
    case NODE_OPERATION:
    case NODE_UNARY_OPERATION:
    case NODE_INT:
    case NODE_SHORT:
    case NODE_FLOAT:
    case NODE_DOUBLE:
    case NODE_CHAR:
    case NODE_IDENTIFIER:
        evaluate_expression(node);
        break;
    case NODE_FUNC_CALL:
        if (strcmp(node->data.func_call.function_name, "yapping") == 0)
        {
            execute_yapping_call(node->data.func_call.arguments);
        }
        else if (strcmp(node->data.func_call.function_name, "yappin") == 0)
        {
            execute_yappin_call(node->data.func_call.arguments);
        }
        else if (strcmp(node->data.func_call.function_name, "baka") == 0)
        {
            execute_baka_call(node->data.func_call.arguments);
        }
        else if (strcmp(node->data.func_call.function_name, "ragequit") == 0)
        {
            execute_ragequit_call(node->data.func_call.arguments);
        }
        else if (strcmp(node->data.func_call.function_name, "chill") == 0)
        {
            execute_chill_call(node->data.func_call.arguments);
        }
        else if (strcmp(node->data.func_call.function_name, "slorp") == 0)
        {
            execute_slorp_call(node->data.func_call.arguments);
        }
        break;
    case NODE_FOR_STATEMENT:
        execute_for_statement(node);
        break;
    case NODE_WHILE_STATEMENT:
        execute_while_statement(node);
        break;
    case NODE_DO_WHILE_STATEMENT:
        execute_do_while_statement(node);
        break;
    case NODE_PRINT_STATEMENT:
    {
        ASTNode *expr = node->data.op.left;
        if (expr->type == NODE_STRING_LITERAL)
        {
            yapping("%s\n", expr->data.name);
        }
        else
        {
            int value = evaluate_expression(expr);
            yapping("%d\n", value);
        }
        break;
    }
    case NODE_ERROR_STATEMENT:
    {
        ASTNode *expr = node->data.op.left;
        if (expr->type == NODE_STRING_LITERAL)
        {
            baka("%s\n", expr->data.name);
        }
        else
        {
            int value = evaluate_expression(expr);
            baka("%d\n", value);
        }
        break;
    }
    case NODE_STATEMENT_LIST:
        execute_statements(node);
        break;
    case NODE_IF_STATEMENT:
        enter_scope();
        if (evaluate_expression(node->data.if_stmt.condition))
        {
            execute_statement(node->data.if_stmt.then_branch);
        }
        else if (node->data.if_stmt.else_branch)
        {
            execute_statement(node->data.if_stmt.else_branch);
        }
        exit_scope();
        break;
    case NODE_SWITCH_STATEMENT:
        execute_switch_statement(node);
        break;
    case NODE_BREAK_STATEMENT:
        // Signal to break out of the current loop/switch
        bruh();
        break;
    case NODE_FUNCTION_DEF:
    {
        const Function *func = create_function(
            node->data.function_def.name,
            node->data.function_def.return_type,
            node->data.function_def.parameters,
            node->data.function_def.body);
        if (!func)
        {
            yyerror("Failed to create function");
            exit(1);
        }
        break;
    }
    case NODE_RETURN:
    {
        handle_return_statement(node->data.op.left);
        break;
    }
    default:
        yyerror("Unknown statement type");
        break;
    }
}

void execute_statements(ASTNode *node)
{
    if (!node) {
        return;
}
    if (node->type != NODE_STATEMENT_LIST)
    {
        execute_statement(node);
        return;
    }
    StatementList *current = node->data.statements;
    while (current)
    {
        execute_statement(current->statement);
        current = current->next;
    }
}

void execute_for_statement(ASTNode *node)
{
    PUSH_JUMP_BUFFER();
    if (setjmp(CURRENT_JUMP_BUFFER()) == 0)
    {
        // Execute initialization once
        enter_scope();
        if (node->data.for_stmt.init)
        {
            execute_statement(node->data.for_stmt.init);
        }

        while (1)
        {
            // Evaluate condition
            enter_scope();
            if (node->data.for_stmt.cond)
            {
                int cond_result = evaluate_expression(node->data.for_stmt.cond);
                if (!cond_result)
                {
                    break;
                }
            }

            // Execute body
            if (node->data.for_stmt.body)
            {
                execute_statement(node->data.for_stmt.body);
            }

            // Execute increment
            if (node->data.for_stmt.incr)
            {
                execute_statement(node->data.for_stmt.incr);
            }
            exit_scope();
        }
        exit_scope();
    }
    POP_JUMP_BUFFER();
}

void execute_while_statement(ASTNode *node)
{
    PUSH_JUMP_BUFFER();
    enter_scope();
    while (evaluate_expression(node->data.while_stmt.cond) && setjmp(CURRENT_JUMP_BUFFER()) == 0)
    {
        enter_scope();
        execute_statement(node->data.while_stmt.body);
        exit_scope();
    }
    exit_scope();
    POP_JUMP_BUFFER();
}

void execute_do_while_statement(ASTNode *node)
{
    PUSH_JUMP_BUFFER();
    enter_scope();
    do
    {
        enter_scope();
        execute_statement(node->data.while_stmt.body);
        exit_scope();
    } while (evaluate_expression(node->data.while_stmt.cond) && setjmp(CURRENT_JUMP_BUFFER()) == 0);
    exit_scope();
    POP_JUMP_BUFFER();
}

ASTNode *create_if_statement_node(ASTNode *condition, ASTNode *then_branch, ASTNode *else_branch)
{
    ASTNode *node = SAFE_MALLOC(ASTNode);
    node->type = NODE_IF_STATEMENT;
    node->data.if_stmt.condition = condition;
    node->data.if_stmt.then_branch = then_branch;
    node->data.if_stmt.else_branch = else_branch;
    return node;
}

ASTNode *create_string_literal_node(const char *string)
{
    ASTNode *node = SAFE_MALLOC(ASTNode);
    node->type = NODE_STRING_LITERAL;
    node->data.name = safe_strdup(string);
    return node;
}

ASTNode *create_switch_statement_node(ASTNode *expression, CaseNode *cases)
{
    ASTNode *node = SAFE_MALLOC(ASTNode);
    node->type = NODE_SWITCH_STATEMENT;
    node->data.switch_stmt.expression = expression;
    node->data.switch_stmt.cases = cases;
    return node;
}

CaseNode *create_case_node(ASTNode *value, ASTNode *statements)
{
    CaseNode *node = SAFE_MALLOC(CaseNode);
    node->value = value;
    node->statements = statements;
    node->next = NULL;
    return node;
}

CaseNode *create_default_case_node(ASTNode *statements)
{
    return create_case_node(NULL, statements); // NULL value indicates default case
}

CaseNode *append_case_list(CaseNode *list, CaseNode *case_node)
{
    if (!list) {
        return case_node;
}
    CaseNode *current = list;
    while (current->next) {
        current = current->next;
}
    current->next = case_node;
    return list;
}

ASTNode *create_break_node()
{
    ASTNode *node = SAFE_MALLOC(ASTNode);
    node->type = NODE_BREAK_STATEMENT;
    node->data.break_stmt = NULL;
    return node;
}

void execute_yapping_call(ArgumentList *args)
{
    if (!args)
    {
        yyerror("No arguments provided for yapping function call");
        exit(EXIT_FAILURE);
    }

    ASTNode *formatNode = args->expr;
    if (formatNode->type != NODE_STRING_LITERAL)
    {
        yyerror("First argument to yapping must be a string literal");
        return;
    }

    const char *format = formatNode->data.name; // The format string
    char buffer[1024];                          // Buffer for the final formatted output
    int buffer_offset = 0;

    ArgumentList *cur = args->next;

    while (*format != '\0')
    {
        if (*format == '%' && cur != NULL)
        {
            // Start extracting the format specifier
            const char *start = format;
            format++; // Move past '%'

            // Extract until a valid specifier character is found
            while (strchr("diouxXfFeEgGaAcspnb%", *format) == NULL && *format != '\0')
            {
                format++;
            }

            if (*format == '\0')
            {
                yyerror("Invalid format specifier");
                exit(EXIT_FAILURE);
            }

            // Copy the format specifier into a temporary buffer
            char specifier[32];
            int length = format - start + 1;
            strncpy(specifier, start, length);
            specifier[length] = '\0';

            // Process the argument based on the format specifier
            ASTNode *expr = cur->expr;
            if (!expr)
            {
                yyerror("Invalid argument in yapping call");
                exit(EXIT_FAILURE);
            }

            if (*format == 'b')
            {
                // Handle boolean values
                bool val = evaluate_expression_bool(expr);
                buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, "%s", val ? "W" : "L");
            }
            else if (strchr("diouxX", *format))
            {
                // Integer or unsigned integer
                bool is_unsigned = expr->modifiers.is_unsigned ||
                                   (expr->type == NODE_IDENTIFIER &&
                                    get_variable_modifiers(expr->data.name).is_unsigned);

                if (is_unsigned)
                {
                    if (is_short_expression(expr))
                    {
                        unsigned short val = evaluate_expression_short(expr);
                        buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                    }
                    else
                    {
                        unsigned int val = (unsigned int)evaluate_expression_int(expr);
                        buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                    }
                }
                else
                {
                    if (is_short_expression(expr))
                    {
                        short val = evaluate_expression_short(expr);
                        buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                    }
                    else
                    {
                        int val = evaluate_expression_int(expr);
                        buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                    }
                }
            }
            else if (strchr("fFeEgGa", *format))
            {
                // Floating-point numbers
                if (expr->type == NODE_ARRAY_ACCESS)
                {
                    // Special handling for array access
                    const char *array_name = expr->data.array.name;
                    int idx = evaluate_expression_int(expr->data.array.index);

                    Variable *var = get_variable(array_name);
                    if (var != NULL)
                    {
                        if (!var->is_array)
                        {
                            yyerror("Not an array!");
                            return;
                        }
                        if (idx < 0 || idx >= var->array_length)
                        {
                            yyerror("Array index out of bounds!");
                            return;
                        }
                        if (var->var_type == VAR_FLOAT)
                        {
                            float val = var->value.farray[idx];
                            buffer_offset += snprintf(buffer + buffer_offset,
                                                      sizeof(buffer) - buffer_offset,
                                                      specifier, val);
                        }
                        else if (var->var_type == VAR_DOUBLE)
                        {
                            double val = var->value.darray[idx];
                            buffer_offset += snprintf(buffer + buffer_offset,
                                                      sizeof(buffer) - buffer_offset,
                                                      specifier, val);
                        }
                        break;
                    }
                }
                else if (is_float_expression(expr))
                {
                    float val = evaluate_expression_float(expr);
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                }
                else if (is_double_expression(expr))
                {
                    double val = evaluate_expression_double(expr);
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                }
                else
                {
                    yyerror("Invalid argument type for floating-point format specifier");
                    exit(EXIT_FAILURE);
                }
            }
            else if (*format == 'c')
            {
                // Character
                int val = evaluate_expression_int(expr);
                buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
            }
            else if (*format == 's')
            {
                // String
                if (expr->type != NODE_STRING_LITERAL)
                {
                    yyerror("Invalid argument type for %s");
                    exit(EXIT_FAILURE);
                }
                buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, expr->data.name);
            }
            else
            {
                yyerror("Unsupported format specifier");
                exit(EXIT_FAILURE);
            }

            cur = cur->next; // Move to the next argument
            format++;        // Move past the format specifier
        }
        else
        {
            // Copy non-format characters to the buffer
            buffer[buffer_offset++] = *format++;
        }

        // Check for buffer overflow
        if (buffer_offset >= sizeof(buffer))
        {
            yyerror("Buffer overflow in yapping call");
            exit(EXIT_FAILURE);
        }
    }

    buffer[buffer_offset] = '\0'; // Null-terminate the string

    // Print the final formatted output
    yapping("%s", buffer);
}

void execute_yappin_call(ArgumentList *args)
{
    if (!args)
    {
        yyerror("No arguments provided for yappin function call");
        exit(EXIT_FAILURE);
    }

    ASTNode *formatNode = args->expr;
    if (formatNode->type != NODE_STRING_LITERAL)
    {
        yyerror("First argument to yappin must be a string literal");
        exit(EXIT_FAILURE);
    }

    const char *format = formatNode->data.name; // The format string
    char buffer[1024];                          // Buffer for the final formatted output
    int buffer_offset = 0;

    ArgumentList *cur = args->next;

    while (*format != '\0')
    {
        if (*format == '%' && cur != NULL)
        {
            // Start extracting the format specifier
            const char *start = format;
            format++; // Move past '%'

            // Extract until a valid specifier character is found
            while (strchr("diouxXfFeEgGaAcspnb%", *format) == NULL && *format != '\0')
            {
                format++;
            }

            if (*format == '\0')
            {
                yyerror("Invalid format specifier");
                exit(EXIT_FAILURE);
            }

            // Copy the format specifier into a temporary buffer
            char specifier[32];
            int length = format - start + 1;
            strncpy(specifier, start, length);
            specifier[length] = '\0';

            // Process the argument based on the format specifier
            ASTNode *expr = cur->expr;
            if (!expr)
            {
                yyerror("Invalid argument in yappin call");
                exit(EXIT_FAILURE);
            }

            if (*format == 'b')
            {
                // Handle boolean values
                bool val = evaluate_expression_bool(expr);
                buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, "%s", val ? "W" : "L");
            }
            else if (strchr("diouxX", *format))
            {
                if (is_short_expression(expr))
                {
                    short val = evaluate_expression_short(expr);
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                }
                else
                {
                    int val = evaluate_expression_int(expr);
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                }
            }
            else if (strchr("fFeEgGa", *format))
            {
                // Handle floating-point numbers
                if (is_float_expression(expr))
                {
                    float val = evaluate_expression_float(expr);
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                }
                else if (is_double_expression(expr))
                {
                    double val = evaluate_expression_double(expr);
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                }
                else
                {
                    yyerror("Invalid argument type for floating-point format specifier");
                    exit(EXIT_FAILURE);
                }
            }
            else if (*format == 'c')
            {
                // Handle character values
                int val = evaluate_expression_int(expr);
                buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
            }
            else if (*format == 's')
            {
                // Handle string literals
                if (expr->type != NODE_STRING_LITERAL)
                {
                    yyerror("Invalid argument type for %s");
                    exit(EXIT_FAILURE);
                }
                buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, expr->data.name);
            }
            else
            {
                yyerror("Unsupported format specifier");
                exit(EXIT_FAILURE);
            }

            cur = cur->next; // Move to the next argument
            format++;        // Move past the format specifier
        }
        else
        {
            // Copy non-format characters to the buffer
            buffer[buffer_offset++] = *format++;
        }

        // Check for buffer overflow
        if (buffer_offset >= sizeof(buffer))
        {
            yyerror("Buffer overflow in yappin call");
            exit(EXIT_FAILURE);
        }
    }

    buffer[buffer_offset] = '\0'; // Null-terminate the string

    // Print the final formatted output
    yappin("%s", buffer);
}

void execute_baka_call(const ArgumentList *args)
{
    if (!args)
    {
        baka("\n");
        return;
    }

    const ASTNode *formatNode = args->expr;
    if (formatNode->type != NODE_STRING_LITERAL)
    {
        yyerror("First argument to yapping must be a string literal");
        return;
    }

    baka(formatNode->data.name);
}

void execute_ragequit_call(const ArgumentList *args)
{
    if (!args)
    {
        yyerror("No arguments provided for ragequit function call");
        exit(EXIT_FAILURE);
    }

    const ASTNode *formatNode = args->expr;
    if (formatNode->type != NODE_INT)
    {
        yyerror("First argument to ragequit must be a integer");
        exit(EXIT_FAILURE);
    }

    ragequit(formatNode->data.ivalue);
}

void execute_chill_call(const ArgumentList *args)
{
    if (!args)
    {
        yyerror("No arguments provided for chill function call");
        exit(EXIT_FAILURE);
    }

    const ASTNode *formatNode = args->expr;
    if (formatNode->type != NODE_INT && !formatNode->modifiers.is_unsigned)
    {
        yyerror("First argument to chill must be a unsigned integer");
        exit(EXIT_FAILURE);
    }

    chill(formatNode->data.ivalue);
}

void execute_slorp_call(const ArgumentList *args)
{
    if (!args || args->expr->type != NODE_IDENTIFIER)
    {
        yyerror("slurp requires a variable identifier");
        return;
    }

    const char *name = args->expr->data.name;
    const Variable *var = get_variable(name);
    if (!var)
    {
        yyerror("Undefined variable");
        return;
    }

    switch (var->var_type)
    {
    case VAR_INT:
    {
        int val = 0;
        val = slorp_int(val);
        set_int_variable(name, val, var->modifiers);
        break;
    }
    case VAR_FLOAT:
    {
        float val = 0.0F;
        val = slorp_float(val);
        set_float_variable(name, val, var->modifiers);
        break;
    }
    case VAR_DOUBLE:
    {
        double val = 0.0;
        val = slorp_double(val);
        set_double_variable(name, val, var->modifiers);
        break;
    }
    case VAR_SHORT:
    {
        short val = 0;
        val = slorp_short(val);
        set_short_variable(name, val, var->modifiers);
        break;
    }
    case VAR_CHAR:
    {
        char val = 0;
        val = slorp_char(val);
        set_int_variable(name, val, var->modifiers);
        break;
    }
    default:
        yyerror("Unsupported type for slorp");
    }
}

void bruh()
{
    LONGJMP();
}

ASTNode *create_default_node(VarType var_type)
{
    switch (var_type)
    {
    case VAR_INT:
        return create_int_node(0);
    case VAR_FLOAT:
        return create_float_node(0.0F);
    case VAR_DOUBLE:
        return create_double_node(0.0);
    case VAR_SHORT:
        return create_short_node(0);
    case VAR_CHAR:
        return create_char_node('\0');
    case VAR_BOOL:
        return create_boolean_node(0);
    default:
        yyerror("Unsupported type for default node");
        exit(1);
    }
}

void *evaluate_array_access(ASTNode *node)
{
    if (!node || node->type != NODE_ARRAY_ACCESS)
    {
        yyerror("Invalid array access node");
        return NULL;
    }

    const char *array_name = node->data.array.name;
    int idx = evaluate_expression_int(node->data.array.index);

    Variable *var = get_variable(array_name);

    if (var != NULL)
    {
        if (!var->is_array)
        {
            yyerror("Not an array!");
            return NULL;
        }
        if (idx < 0 || idx >= var->array_length)
        {
            yyerror("Array index out of bounds!");
            return NULL;
        }

        // Allocate and return value based on type
        void *result = SAFE_MALLOC(double); // Use largest possible type
        switch (var->var_type)
        {
        case VAR_DOUBLE:
            *(double *)result = var->value.darray[idx];
            break;
        case VAR_FLOAT:
            *(double *)result = (double)var->value.farray[idx];
            break;
        case VAR_INT:
            *(double *)result = (double)var->value.iarray[idx];
            break;
        case VAR_SHORT:
            *(double *)result = (double)var->value.sarray[idx];
            break;
        case VAR_CHAR:
            *(double *)result = (double)var->value.carray[idx];
            break;
        case VAR_BOOL:
            *(double *)result = (double)var->value.barray[idx];
            break;
        default:
            yyerror("Unsupported array type");
            return NULL;
        }
        return result;
    }
    yyerror("Undefined array variable");
    return NULL;
}

ExpressionList *create_expression_list(ASTNode *expr)
{
    ExpressionList *list = SAFE_MALLOC(ExpressionList);
    if (!list)
    {
        yyerror("Failed to allocate memory for expression list");
        exit(1);
    }
    list->expr = expr;
    list->next = list;
    list->prev = list;
    return list;
}

ExpressionList *append_expression_list(ExpressionList *list, ASTNode *expr)
{
    ExpressionList *new_node = SAFE_MALLOC(ExpressionList);
    if (!new_node)
    {
        yyerror("Failed to allocate memory for expression list");
        exit(1);
    }
    new_node->expr = expr;

    if (!list)
    {
        new_node->next = new_node;
        new_node->prev = new_node;
        return new_node;
    }

    new_node->next = list;
    new_node->prev = list->prev;
    list->prev->next = new_node;
    list->prev = new_node;
    return list;
}

size_t count_expression_list(ExpressionList *list)
{
    if (!list) {
        return 0;
}
    size_t count = 1;
    ExpressionList *current = list->next;
    do
    {
        count++;
        current = current->next;
    } while (current != list);
    return count;
}

void free_expression_list(ExpressionList *list)
{
    if (!list) {
        return;
}
    ExpressionList *current = list->next;
    while (current != list)
    {
        ExpressionList *next = current->next;
        SAFE_FREE(current);
        current = next;
    }
    SAFE_FREE(list);
}

void populate_array_variable(const char *name, ExpressionList *list)
{
    const Variable *var = get_variable(name);
    if (var != NULL)
    {
        if (!var->is_array)
        {
            yyerror("Not an array!");
            return;
        }
        if (var->array_length < count_expression_list(list))
        {
            yyerror("Too many elements in array initialization");
            exit(1);
        }

        size_t array_length = var->array_length;
        VarType var_type = var->var_type;

        ExpressionList *current = list;
        for (size_t index = 0; index < array_length; index++)
        {
            switch (var_type)
            {
            case VAR_INT:
                var->value.iarray[index] = evaluate_expression_int(current->expr);
                break;
            case VAR_FLOAT:
                var->value.farray[index] = evaluate_expression_float(current->expr);
                break;
            case VAR_DOUBLE:
                var->value.darray[index] = evaluate_expression_double(current->expr);
                break;
            case VAR_SHORT:
                var->value.sarray[index] = evaluate_expression_short(current->expr);
                break;
            case VAR_CHAR:
                var->value.carray[index] = (char)evaluate_expression_int(current->expr);
                break;
            case VAR_BOOL:
                var->value.barray[index] = evaluate_expression_bool(current->expr);
                break;
            default:
                yyerror("Unsupported array type");
                return;
            }

            SAFE_FREE(current->expr);
            current = current->next;
            if (current == list) {
                break;
}
        }

        return;
    }
    yyerror("Undefined array variable");
}

void free_statement_list(StatementList *list)
{
    while (list)
    {
        StatementList *next = list->next;

        // Free the current list node
        SAFE_FREE(list);

        // Move to the next node
        list = next;
    }
}

void free_ast(ASTNode *node)
{
    if (!node) {
        return;
}

    switch (node->type)
    {
    case NODE_IDENTIFIER:
        SAFE_FREE(node->data.name);
        break;

    case NODE_OPERATION:
    case NODE_ASSIGNMENT:
    case NODE_DECLARATION:
        free_ast(node->data.op.left);
        free_ast(node->data.op.right);
        break;

    case NODE_UNARY_OPERATION:
        free_ast(node->data.unary.operand);
        break;

    case NODE_STATEMENT_LIST:
    {
        StatementList *current = node->data.statements;
        while (current)
        {
            StatementList *next = current->next;
            if (current->statement)
            {
                free_ast(current->statement);
            }
            SAFE_FREE(current);
            current = next;
        }
        break;
    }

    case NODE_SWITCH_STATEMENT:
        if (node->data.switch_stmt.expression) {
            free_ast(node->data.switch_stmt.expression);
}
        if (node->data.switch_stmt.cases)
        {
            CaseNode *current = node->data.switch_stmt.cases;
            while (current)
            {
                CaseNode *next = current->next;
                if (current->value) {
                    free_ast(current->value);
}
                if (current->statements) {
                    free_ast(current->statements);
}
                SAFE_FREE(current);
                current = next;
            }
        }
        break;

    case NODE_FOR_STATEMENT:
        if (node->data.for_stmt.init) {
            free_ast(node->data.for_stmt.init);
}
        if (node->data.for_stmt.cond) {
            free_ast(node->data.for_stmt.cond);
}
        if (node->data.for_stmt.incr) {
            free_ast(node->data.for_stmt.incr);
}
        if (node->data.for_stmt.body) {
            free_ast(node->data.for_stmt.body);
}
        break;

    case NODE_WHILE_STATEMENT:
    case NODE_DO_WHILE_STATEMENT:
        if (node->data.while_stmt.cond) {
            free_ast(node->data.while_stmt.cond);
}
        if (node->data.while_stmt.body) {
            free_ast(node->data.while_stmt.body);
}
        break;

    case NODE_ARRAY_ACCESS:
        if (node->data.array.name)
        {
            SAFE_FREE(node->data.array.name);
        }
        if (node->data.array.index)
        {
            free_ast(node->data.array.index);
        }
        break;

    case NODE_IF_STATEMENT:
        if (node->data.if_stmt.condition) {
            free_ast(node->data.if_stmt.condition);
}
        if (node->data.if_stmt.then_branch) {
            free_ast(node->data.if_stmt.then_branch);
}
        if (node->data.if_stmt.else_branch) {
            free_ast(node->data.if_stmt.else_branch);
}
        break;

    case NODE_SIZEOF:
        if (node->data.sizeof_stmt.expr) {
            free_ast(node->data.sizeof_stmt.expr);
}
        break;

    case NODE_BREAK_STATEMENT:
        // Nothing additional to free
        break;

    case NODE_FUNC_CALL:
        if (node->data.func_call.function_name)
        {
            SAFE_FREE(node->data.func_call.function_name);
        }
        ArgumentList *current_arg = node->data.func_call.arguments;
        while (current_arg)
        {
            ArgumentList *next_arg = current_arg->next;
            if (current_arg->expr)
            {
                free_ast(current_arg->expr);
            }
            SAFE_FREE(current_arg);
            current_arg = next_arg;
        }
        break;

    case NODE_STRING_LITERAL:
        if (node->data.name)
        {
            SAFE_FREE(node->data.name);
        }
        break;

    case NODE_INT:
    case NODE_SHORT:
    case NODE_FLOAT:
    case NODE_DOUBLE:
    case NODE_BOOLEAN:
        // These nodes don't have additional allocations
        break;

    case NODE_ERROR_STATEMENT:
    case NODE_PRINT_STATEMENT:
        if (node->data.op.left)
        {
            free_ast(node->data.op.left);
        }
        break;
    case NODE_FUNCTION_DEF:
        SAFE_FREE(node->data.function_def.name);
        // Free parameters
        if (node->data.function_def.body)
        {
            free_ast(node->data.function_def.body);
        }
        break;
    case NODE_RETURN:
        if (node->data.op.left)
        {
            free_ast(node->data.op.left);
        }
        break;
    default:
        fprintf(stderr, "Warning: Unknown node type in free_ast: %d\n", node->type);
        break;
    }

    // Free the node itself
    SAFE_FREE(node);
}

Scope *create_scope(Scope *parent)
{
    Scope *scope = SAFE_MALLOC(Scope);
    if (!scope)
    {
        yyerror("Failed to allocate memory for scope");
        SAFE_FREE(scope);
        exit(1);
    }
    scope->variables = hm_new();
    scope->parent = parent;
    return scope;
}

Variable *get_variable(const char *name)
{
    Scope *scope = current_scope;
    while (scope)
    {
        Variable *var = hm_get(scope->variables, name, strlen(name));
        if (var)
        {
            return var;
        }
        scope = scope->parent;
    }
    return NULL;
}

void exit_scope()
{
    if (!current_scope)
    {
        yyerror("No scope to exit");
        exit(1);
    }
    Scope *parent = current_scope->parent;
    hm_free(current_scope->variables);
    SAFE_FREE(current_scope);
    current_scope = parent;
}

void free_scope(Scope *scope)
{
    if (!scope) {
        return;
}
    hm_free(scope->variables);
    free_scope(scope->parent);
    SAFE_FREE(scope);
}
void enter_scope()
{
    current_scope = create_scope(current_scope);
}
Variable *variable_new(char *name)
{
    Variable *var = SAFE_MALLOC(Variable);
    if (!var)
    {
        yyerror("Failed to allocate memory for variable");
        exit(1);
    }
    var->name = name;
    var->is_array = false;
    return var;
}

void add_variable_to_scope(const char *name, Variable *var)
{
    if (!current_scope)
    {
        yyerror("No scope to add variable to");
        exit(1);
    }
    const Variable *existing = hm_get(current_scope->variables, name, strlen(name));
    if (existing)
    {
        yyerror("Variable already exists in current scope");
        SAFE_FREE(var);
        exit(1);
    }

    hm_put(current_scope->variables, name, strlen(name), var, sizeof(Variable));
}

ASTNode *create_return_node(ASTNode *expr)
{
    ASTNode *node = SAFE_MALLOC(ASTNode);
    if (!node)
    {
        yyerror("Memory allocation failed");
        return NULL;
    }
    node->type = NODE_RETURN;
    node->data.op.left = expr; // Store return expression in left operand
    return node;
}

Function *create_function(const char *name, VarType return_type, Parameter *params, ASTNode *body)
{
    Function *func = SAFE_MALLOC(Function);
    if (!func)
    {
        yyerror("Failed to allocate memory for function");
        return NULL;
    }

    func->name = safe_strdup(name);
    func->return_type = return_type;
    func->parameters = params;
    func->body = body;
    func->next = function_table;
    function_table = func;

    return func;
}

void execute_function_call(const char *name, ArgumentList *args)
{
    // Find function in function table
    Function *func = function_table;
    while (func)
    {
        if (strcmp(func->name, name) == 0)
        {
            // Create new scope for function
            enter_scope();

            // Process arguments and parameters
            ArgumentList *curr_arg = args;
            Parameter *curr_param = func->parameters;
            current_return_value.type = func->return_type;

            // reverse the order of parameters
            Parameter *prev = NULL;
            Parameter *next = NULL;
            while (curr_param)
            {
                next = curr_param->next;
                curr_param->next = prev;
                prev = curr_param;
                curr_param = next;
            }
            curr_param = prev;

            while (curr_arg && curr_param)
            {
                // Create variable for parameter
                Variable *var = variable_new(curr_param->name);
                var->var_type = curr_param->type;
                add_variable_to_scope(curr_param->name, var);

                switch (curr_param->type)
                {
                case VAR_INT:
                    set_int_variable(curr_param->name, evaluate_expression_int(curr_arg->expr), get_current_modifiers());
                    break;
                case VAR_FLOAT:
                    set_float_variable(curr_param->name, evaluate_expression_float(curr_arg->expr), get_current_modifiers());
                    break;
                case VAR_DOUBLE:
                    set_double_variable(curr_param->name, evaluate_expression_double(curr_arg->expr), get_current_modifiers());
                    break;
                case VAR_BOOL:
                    set_bool_variable(curr_param->name, evaluate_expression_bool(curr_arg->expr), get_current_modifiers());
                    break;
                case VAR_SHORT:
                    set_short_variable(curr_param->name, evaluate_expression_short(curr_arg->expr), get_current_modifiers());
                    break;
                case VAR_CHAR:
                    set_int_variable(curr_param->name, evaluate_expression_int(curr_arg->expr), get_current_modifiers());
                    break;
                case NONE:
                    break;
                default:
                    yyerror("Unsupported parameter type");
                    exit_scope();
                    return;
                }

                Parameter *tmp = curr_param;
                curr_arg = curr_arg->next;
                curr_param = curr_param->next;
                SAFE_FREE(var);
                SAFE_FREE(tmp->name);
                SAFE_FREE(tmp);
            }

            if (curr_arg || curr_param)
            {
                yyerror("Mismatched number of arguments and parameters");
                exit_scope();
                return;
            }

            // Set up return handling
            current_return_value.has_value = false;
            PUSH_JUMP_BUFFER();
            if (setjmp(CURRENT_JUMP_BUFFER()) == 0)
            {
                execute_statement(func->body);
            }

            POP_JUMP_BUFFER();
            // Clean up scope
            exit_scope();
            return;
        }
        func = func->next;
    }

    yyerror("Undefined function");
}

void handle_return_statement(ASTNode *expr)
{
    current_return_value.has_value = true;
    if (expr)
    {
        switch (current_return_value.type)
        {
        case VAR_INT:
            current_return_value.value.ivalue = evaluate_expression_int(expr);
            break;
        case VAR_FLOAT:
            current_return_value.value.fvalue = evaluate_expression_float(expr);
            break;
        case VAR_DOUBLE:
            current_return_value.value.dvalue = evaluate_expression_double(expr);
            break;
        case VAR_BOOL:
            current_return_value.value.bvalue = evaluate_expression_bool(expr);
            break;
        case VAR_SHORT:
            current_return_value.value.svalue = evaluate_expression_short(expr);
            break;
        default:
            yyerror("Unsupported return type");
            exit(1);
        }
    }
    // skibidi main function do not have jump buffer

    if (jump_buffer && CURRENT_JUMP_BUFFER() != NULL)
        LONGJMP();
}

Parameter *create_parameter(const char *name, VarType type, Parameter *next)
{
    Parameter *param = SAFE_MALLOC(Parameter);
    if (!param)
    {
        yyerror("Failed to allocate memory for parameter");
        return NULL;
    }

    param->name = safe_strdup(name);
    param->type = type;
    param->next = next;

    return param;
}

ASTNode *create_function_def_node(const char *name, VarType return_type, Parameter *params, ASTNode *body)
{
    ASTNode *node = SAFE_MALLOC(ASTNode);
    if (!node)
    {
        yyerror("Failed to allocate memory for function definition node");
        return NULL;
    }

    node->type = NODE_FUNCTION_DEF;
    node->data.function_def.name = safe_strdup(name);
    node->data.function_def.return_type = return_type;
    node->data.function_def.parameters = params;
    node->data.function_def.body = body;

    // Add function to global function table
    create_function(name, return_type, params, body);

    return node;
}

void free_parameters(Parameter *param)
{
    while (param)
    {
        Parameter *next = param->next;
        SAFE_FREE(param->name);
        SAFE_FREE(param);
        param = next;
    }
}

void free_function_table(void)
{
    Function *f = function_table;
    while (f)
    {
        Function *next = f->next;

        // Safe to free f->name: it's a separate safe_strdup from the AST's name.
        SAFE_FREE(f->name);

        // DO NOT free f->parameters or f->body here,
        // because those pointers belong to the AST and
        // are already freed in free_ast(root).

        SAFE_FREE(f);
        f = next;
    }
    function_table = NULL;
}
