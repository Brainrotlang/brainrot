/* ast.c */

#include "ast.h"
#include <stdbool.h>
#include <setjmp.h>
#include <math.h>
#include <limits.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>

static jmp_buf break_env;

TypeModifiers current_modifiers = {false, false, false, false, false};
extern VarType current_var_type;

Variable symbol_table[MAX_VARS];
int var_count = 0;

// Symbol table functions
bool set_variable(const char *name, void *value, VarType type, TypeModifiers mods)
{
    // Search for an existing variable
    for (int i = 0; i < var_count; i++)
    {
        if (strcmp(symbol_table[i].name, name) == 0)
        {
            symbol_table[i].var_type = type;
            symbol_table[i].modifiers = mods;

            switch (type)
            {
            case VAR_INT:
                symbol_table[i].value.ivalue = *(int *)value;
                break;
            case VAR_SHORT:
                symbol_table[i].value.svalue = *(short *)value;
                break;
            case VAR_FLOAT:
                symbol_table[i].value.fvalue = *(float *)value;
                break;
            case VAR_DOUBLE:
                symbol_table[i].value.dvalue = *(double *)value;
                break;
            case VAR_BOOL:
                symbol_table[i].value.bvalue = *(bool *)value;
                break;
            case VAR_CHAR:
                symbol_table[i].value.ivalue = *(char *)value;
                break;
            }
            return true;
        }
    }

    // Add a new variable if it doesn't exist
    if (var_count < MAX_VARS)
    {
        symbol_table[var_count].name = strdup(name);
        symbol_table[var_count].var_type = type;
        symbol_table[var_count].modifiers = mods;

        switch (type)
        {
        case VAR_INT:
            symbol_table[var_count].value.ivalue = *(int *)value;
            break;
        case VAR_SHORT:
            symbol_table[var_count].value.svalue = *(short *)value;
            break;
        case VAR_FLOAT:
            symbol_table[var_count].value.fvalue = *(float *)value;
            break;
        case VAR_DOUBLE:
            symbol_table[var_count].value.dvalue = *(double *)value;
            break;
        case VAR_BOOL:
            symbol_table[var_count].value.bvalue = *(bool *)value;
            break;
        case VAR_CHAR:
            symbol_table[var_count].value.ivalue = *(char *)value;
            break;
        default:
            break;
        }
        var_count++;
        return true;
    }
    return false; // Symbol table is full
}

bool set_int_variable(const char *name, int value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_INT, mods);
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
extern int get_variable(char *name);
extern void yyerror(const char *s);
extern void ragequit(int exit_code);
extern void chill(unsigned int seconds);
extern void yapping(const char *format, ...);
extern void yappin(const char *format, ...);
extern void baka(const char *format, ...);
extern TypeModifiers get_variable_modifiers(const char *name);
extern int yylineno;

/* Function implementations */

bool check_and_mark_identifier(ASTNode *node, const char *contextErrorMessage)
{
    if (!node->alreadyChecked)
    {
        node->alreadyChecked = true;
        node->isValidSymbol = false;

        // Do the table lookup
        for (int i = 0; i < var_count; i++)
        {
            if (strcmp(symbol_table[i].name, node->data.name) == 0)
            {
                node->isValidSymbol = true;
                break;
            }
        }

        if (!node->isValidSymbol)
        {
            yylineno = yylineno - 2;
            yyerror(contextErrorMessage);
        }
    }

    return node->isValidSymbol;
}

void execute_switch_statement(ASTNode *node)
{
    int switch_value = evaluate_expression(node->data.switch_stmt.expression);
    CaseNode *current_case = node->data.switch_stmt.cases;
    int matched = 0;

    if (setjmp(break_env) == 0)
    {
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
                if (matched || !matched)
                {
                    execute_statements(current_case->statements);
                    break;
                }
            }
            current_case = current_case->next;
        }
    }
    else
    {
        // Break encountered; do nothing
    }
}

static ASTNode *create_node(NodeType type, VarType var_type, TypeModifiers modifiers)
{
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node)
    {
        yyerror("Error: Memory allocation failed for ASTNode.\n");
        exit(EXIT_FAILURE);
    }
    node->type = type;
    node->var_type = var_type;
    node->modifiers = modifiers;
    node->alreadyChecked = false;
    node->isValidSymbol = false;
    return node;
}

ASTNode *create_int_node(int value)
{
    ASTNode *node = create_node(NODE_INT, VAR_INT, current_modifiers);
    SET_DATA_INT(node, value);
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

ASTNode *create_identifier_node(char *name)
{
    ASTNode *node = create_node(NODE_IDENTIFIER, NONE, current_modifiers);
    SET_DATA_NAME(node, name);
    return node;
}

ASTNode *create_assignment_node(char *name, ASTNode *expr)
{
    ASTNode *node = create_node(NODE_ASSIGNMENT, NONE, get_current_modifiers());
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

ASTNode *create_function_call_node(char *func_name, ArgumentList *args)
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

ASTNode *create_sizeof_node(char *name)
{
    ASTNode *node = create_node(NODE_SIZEOF, NONE, current_modifiers);
    SET_DATA_NAME(node, name);
    return node;
}

// @param promotion: 0 for no promotion, 1 for promotion to double 2 for promotion to float
void *handle_identifier(ASTNode *node, const char *contextErrorMessage, int promote)
{
    if (!check_and_mark_identifier(node, contextErrorMessage))
        exit(1);

    char *name = node->data.name;
    for (int i = 0; i < var_count; i++)
    {
        if (strcmp(symbol_table[i].name, name) == 0)
        {
            static Value promoted_value; // Static variable for holding promoted value.
            if (promote == 1)
            {

                switch (symbol_table[i].var_type)
                {
                case VAR_DOUBLE:
                    return &symbol_table[i].value.dvalue;
                case VAR_FLOAT:
                    promoted_value.dvalue = (double)symbol_table[i].value.fvalue;
                    return &promoted_value;
                case VAR_INT:
                case VAR_CHAR:
                case VAR_SHORT:
                    promoted_value.dvalue = (double)symbol_table[i].value.svalue;
                    return &promoted_value;
                case VAR_BOOL:
                    promoted_value.dvalue = (double)symbol_table[i].value.ivalue;
                    return &promoted_value;
                default:
                    yyerror("Unsupported variable type");
                    return NULL;
                }
            }
            else if (promote == 2)
            {
                switch (symbol_table[i].var_type)
                {
                case VAR_DOUBLE:
                    promoted_value.fvalue = (float)symbol_table[i].value.dvalue;
                    return &promoted_value;
                case VAR_FLOAT:
                    return &symbol_table[i].value.fvalue;
                case VAR_INT:
                case VAR_CHAR:
                case VAR_SHORT:
                    promoted_value.fvalue = (float)symbol_table[i].value.svalue;
                case VAR_BOOL:
                    promoted_value.fvalue = (float)symbol_table[i].value.ivalue;
                    return &promoted_value;
                default:
                    yyerror("Unsupported variable type");
                    return NULL;
                }
            }
            else
            {
                switch (symbol_table[i].var_type)
                {
                case VAR_DOUBLE:
                    return &symbol_table[i].value.dvalue;
                case VAR_FLOAT:
                    return &symbol_table[i].value.fvalue;
                case VAR_INT:
                case VAR_CHAR:
                case VAR_SHORT:
                    return &symbol_table[i].value.svalue;
                case VAR_BOOL:
                    return &symbol_table[i].value.ivalue;
                default:
                    yyerror("Unsupported variable type");
                    return NULL;
                }
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
    case NODE_IDENTIFIER:
    {
        // Look up the variable type in the symbol table
        for (int i = 0; i < var_count; i++)
        {
            if (strcmp(symbol_table[i].name, node->data.name) == 0)
            {
                return symbol_table[i].var_type;
            }
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
        if (left_type == VAR_DOUBLE || right_type == VAR_DOUBLE)
            return VAR_DOUBLE;
        else if (left_type == VAR_FLOAT || right_type == VAR_FLOAT)
            return VAR_FLOAT;
        else
            return VAR_INT;
    }
    case NODE_UNARY_OPERATION:
    {
        return get_expression_type(node->data.unary.operand);
    }
    default:
        yyerror("Unknown node type in get_expression_type");
        return NONE;
    }
}

void *handle_binary_operation(ASTNode *node, int result_type)
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
    int promoted_type = VAR_INT;
    if (left_type == VAR_DOUBLE || right_type == VAR_DOUBLE)
        promoted_type = VAR_DOUBLE;
    else if (left_type == VAR_FLOAT || right_type == VAR_FLOAT)
        promoted_type = VAR_FLOAT;

    // Allocate and evaluate operands based on promoted type.
    switch (promoted_type)
    {
    case VAR_INT:
        left_value = calloc(1, sizeof(int));
        right_value = calloc(1, sizeof(int));
        *(int *)left_value = evaluate_expression_int(node->data.op.left);
        *(int *)right_value = evaluate_expression_int(node->data.op.right);
        break;

    case VAR_FLOAT:
        left_value = calloc(1, sizeof(float));
        right_value = calloc(1, sizeof(float));
        *(float *)left_value = (left_type == VAR_INT)
                                   ? (float)evaluate_expression_int(node->data.op.left)
                                   : evaluate_expression_float(node->data.op.left);
        *(float *)right_value = (right_type == VAR_INT)
                                    ? (float)evaluate_expression_int(node->data.op.right)
                                    : evaluate_expression_float(node->data.op.right);
        break;

    case VAR_DOUBLE:
        left_value = calloc(1, sizeof(double));
        right_value = calloc(1, sizeof(double));
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

    default:
        yyerror("Unsupported type promotion");
        return NULL;
    }

    // Perform the operation and allocate the result.
    void *result = calloc(1, (promoted_type == VAR_DOUBLE)  ? sizeof(double)
                             : (promoted_type == VAR_FLOAT) ? sizeof(float)
                             : (promoted_type == VAR_SHORT) ? sizeof(short)
                                                            : sizeof(int));
    switch (node->data.op.op)
    {
    case OP_PLUS:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value + *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
        {
            *(float *)result = *(float *)left_value + *(float *)right_value;
        }
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value + *(double *)right_value;
        break;

    case OP_MINUS:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value - *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
        {
            *(float *)result = *(float *)left_value - *(float *)right_value;
        }
        else if (promoted_type == VAR_DOUBLE)
        {
            volatile double res = *(double *)left_value - *(double *)right_value;
            *(double *)result = res;
        }

        break;

    case OP_TIMES:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value * *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value * *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value * *(double *)right_value;
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
                    *(float *)result = 0.0f / 0.0f; // NaN
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
                    *(double *)result = 0.0 / 0.0; // NaN
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
        else
        {
            yyerror("Modulo operation is only supported for integers");
            *(int *)result = 0;
        }
        break;
    case OP_LT:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value < *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value < *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value < *(double *)right_value;
        break;

    case OP_GT:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value > *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value > *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value > *(double *)right_value;
        break;

    case OP_LE:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value <= *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value <= *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value <= *(double *)right_value;
        break;

    case OP_GE:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value >= *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value >= *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value >= *(double *)right_value;
        break;

    case OP_EQ:

        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value == *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value == *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value == *(double *)right_value;
        break;

    case OP_NE:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value != *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value != *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value != *(double *)right_value;
        break;

    default:
        yyerror("Unsupported binary operator");
        free(result);
        result = NULL;
    }

    free(left_value);
    free(right_value);

    return result;
}

void *handle_unary_expression(ASTNode *node, void *operand_value, int operand_type)
{
    switch (node->data.unary.op)
    {
    case OP_NEG:
        if (operand_type == VAR_INT)
        {
            int *result = malloc(sizeof(int));
            *result = -(*(int *)operand_value);
            return result;
        }
        else if (operand_type == VAR_SHORT)
        {
            short *result = malloc(sizeof(short));
            *result = !(*(short *)operand_value);
            return result;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float *result = malloc(sizeof(float));
            *result = -(*(float *)operand_value);
            return result;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double *result = malloc(sizeof(double));
            *result = -(*(double *)operand_value);
            return result;
        }
        else if (operand_type == VAR_BOOL)
        {
            bool *result = malloc(sizeof(bool));
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
            int old_value = *(int *)operand_value;
            set_int_variable(node->data.unary.operand->data.name, old_value + 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else if (operand_type == VAR_SHORT)
        {
            short old_value = *(short *)operand_value;
            set_short_variable(node->data.unary.operand->data.name, old_value + 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float old_value = *(float *)operand_value;
            set_float_variable(node->data.unary.operand->data.name, old_value + 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double old_value = *(double *)operand_value;
            set_double_variable(node->data.unary.operand->data.name, old_value + 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else
        {
            yyerror("Invalid type for pre-increment");
            return NULL;
        }
    case OP_PRE_DEC:
        if (operand_type == VAR_INT)
        {
            int old_value = *(int *)operand_value;
            set_int_variable(node->data.unary.operand->data.name, old_value - 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else if (operand_type == VAR_SHORT)
        {
            short old_value = *(short *)operand_value;
            set_short_variable(node->data.unary.operand->data.name, old_value - 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float old_value = *(float *)operand_value;
            set_float_variable(node->data.unary.operand->data.name, old_value - 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double old_value = *(double *)operand_value;
            set_double_variable(node->data.unary.operand->data.name, old_value - 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else
        {
            yyerror("Invalid type for pre-decrement");
            return NULL;
        }
    case OP_POST_INC:
        if (operand_type == VAR_INT)
        {
            int old_value = *(int *)operand_value;
            set_int_variable(node->data.unary.operand->data.name, old_value + 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else if (operand_type == VAR_SHORT)
        {
            short old_value = *(short *)operand_value;
            set_short_variable(node->data.unary.operand->data.name, old_value + 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float old_value = *(float *)operand_value;
            set_float_variable(node->data.unary.operand->data.name, old_value + 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double old_value = *(double *)operand_value;
            set_double_variable(node->data.unary.operand->data.name, old_value + 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else
        {
            yyerror("Invalid type for post-increment");
            return NULL;
        }
    case OP_POST_DEC:
        if (operand_type == VAR_INT)
        {
            int old_value = *(int *)operand_value;
            set_int_variable(node->data.unary.operand->data.name, old_value - 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else if (operand_type == VAR_SHORT)
        {
            short old_value = *(short *)operand_value;
            set_short_variable(node->data.unary.operand->data.name, old_value - 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float old_value = *(float *)operand_value;
            set_float_variable(node->data.unary.operand->data.name, old_value - 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double old_value = *(double *)operand_value;
            set_double_variable(node->data.unary.operand->data.name, old_value - 1, get_variable_modifiers(node->data.unary.operand->data.name));
            return operand_value;
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
    if (!node)
        return 0.0f;

    switch (node->type)
    {
    case NODE_FLOAT:
        return node->data.fvalue;
    case NODE_DOUBLE:
        return (float)node->data.dvalue;
    case NODE_INT:
        return (float)node->data.ivalue;
    case NODE_IDENTIFIER:
    {
        return *(float *)handle_identifier(node, "Undefined variable", 1);
    }
    case NODE_OPERATION:
    {
        int result_type = get_expression_type(node);
        void *result = handle_binary_operation(node, result_type);
        float result_float = 0.0f;
        result_float = (result_type == VAR_INT)
                           ? (float)(*(int *)result)
                       : (result_type == VAR_FLOAT)
                           ? *(float *)result
                           : (float)(*(double *)result);
        free(result);
        return result_float;
    }
    case NODE_UNARY_OPERATION:
    {
        float operand = evaluate_expression_float(node->data.unary.operand);
        float *result = (float *)handle_unary_expression(node, &operand, VAR_FLOAT);
        return *result;
    }
    default:
        yyerror("Invalid float expression");
        return 0.0f;
    }
}

double evaluate_expression_double(ASTNode *node)
{
    if (!node)
        return 0.0L;

    switch (node->type)
    {
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
        void *result = handle_binary_operation(node, result_type);
        double result_double = 0.0L;
        result_double = (result_type == VAR_INT)
                            ? (double)(*(int *)result)
                        : (result_type == VAR_FLOAT)
                            ? (double)(*(float *)result)
                            : *(double *)result;
        free(result);
        return result_double;
    }
    case NODE_UNARY_OPERATION:
    {
        double operand = evaluate_expression_double(node->data.unary.operand);
        double *result = (double *)handle_unary_expression(node, &operand, VAR_DOUBLE);
        return *result;
    }
    default:
        yyerror("Invalid double expression");
        return 0.0L;
    }
}

short evaluate_expression_short(ASTNode *node)
{
    if (!node)
        return 0;

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
        char *name = node->data.name;
        for (int i = 0; i < var_count; i++)
        {
            if (strcmp(symbol_table[i].name, name) == 0)
            {
                if (symbol_table[i].var_type == VAR_FLOAT)
                {
                    return sizeof(float);
                }
                else if (symbol_table[i].var_type == VAR_DOUBLE)
                {
                    return sizeof(double);
                }
                else if (symbol_table[i].modifiers.is_unsigned && symbol_table[i].var_type == VAR_INT)
                {
                    return sizeof(unsigned int);
                }
                else if (symbol_table[i].var_type == VAR_BOOL)
                {
                    return sizeof(bool);
                }
                else if (symbol_table[i].modifiers.is_unsigned && symbol_table[i].var_type == VAR_SHORT)
                {
                    return sizeof(unsigned short);
                }
                else if (symbol_table[i].var_type == VAR_SHORT)
                {
                    return sizeof(short);
                }
                else if (symbol_table[i].var_type == VAR_INT)
                {
                    return sizeof(int);
                }
                else
                {
                    yyerror("Undefined variable in sizeof");
                }
            }
        }
        yyerror("Undefined variable in sizeof");
        return 0;
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
        void *result = handle_binary_operation(node, result_type);
        short result_short = 0;
        result_short = (result_type == VAR_SHORT)
                           ? *(short *)result
                       : (result_type == VAR_FLOAT)
                           ? (short)(*(float *)result)
                           : (short)(*(double *)result);
        free(result);
        return result_short;
    }
    case NODE_UNARY_OPERATION:
    {
        short operand = evaluate_expression_short(node->data.unary.operand);
        short *result = (short *)handle_unary_expression(node, &operand, VAR_SHORT);
        return *result;
    }
    default:
        yyerror("Invalid short expression");
        return 0;
    }
}

int evaluate_expression_int(ASTNode *node)
{
    if (!node)
        return 0;

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
        char *name = node->data.name;
        for (int i = 0; i < var_count; i++)
        {
            if (strcmp(symbol_table[i].name, name) == 0)
            {
                if (symbol_table[i].var_type == VAR_FLOAT)
                {
                    return sizeof(float);
                }
                else if (symbol_table[i].var_type == VAR_DOUBLE)
                {
                    return sizeof(double);
                }
                else if (symbol_table[i].modifiers.is_unsigned && symbol_table[i].var_type == VAR_INT)
                {
                    return sizeof(unsigned int);
                }
                else if (symbol_table[i].var_type == VAR_BOOL)
                {
                    return sizeof(bool);
                }
                else if (symbol_table[i].modifiers.is_unsigned && symbol_table[i].var_type == VAR_SHORT)
                {
                    return sizeof(unsigned short);
                }
                else if (symbol_table[i].var_type == VAR_SHORT)
                {
                    return sizeof(short);
                }
                else if (symbol_table[i].var_type == VAR_INT)
                {
                    return sizeof(int);
                }
                else
                {
                    yyerror("Undefined variable in sizeof");
                }
            }
        }
        yyerror("Undefined variable in sizeof");
        return 0;
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
        void *result = handle_binary_operation(node, result_type);
        int result_int = 0;
        result_int = (result_type == VAR_INT)
                         ? *(int *)result
                     : (result_type == VAR_FLOAT)
                         ? (int)(*(float *)result)
                         : (int)(*(double *)result);
        free(result);
        return result_int;
    }
    case NODE_UNARY_OPERATION:
    {
        int operand = evaluate_expression_int(node->data.unary.operand);
        int *result = (int *)handle_unary_expression(node, &operand, VAR_INT);
        return *result;
    }
    default:
        yyerror("Invalid integer expression");
        return 0;
    }
}

bool evaluate_expression_bool(ASTNode *node)
{
    if (!node)
        return 0;

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
        void *result = handle_binary_operation(node, result_type);
        bool result_bool = 0;
        result_bool = (result_type == VAR_INT)
                          ? (bool)(*(int *)result)
                      : (result_type == VAR_FLOAT)
                          ? (bool)(*(float *)result)
                          : (bool)(*(double *)result);
        free(result);
        return result_bool;
    }
    case NODE_UNARY_OPERATION:
    {
        bool operand = evaluate_expression_bool(node->data.unary.operand);
        bool *result = (bool *)handle_unary_expression(node, &operand, VAR_BOOL);
        return *result;
    }
    default:
        yyerror("Invalid boolean expression");
        return 0;
    }
}

ArgumentList *create_argument_list(ASTNode *expr, ArgumentList *existing_list)
{
    ArgumentList *new_node = malloc(sizeof(ArgumentList));
    new_node->expr = expr;
    new_node->next = NULL;

    if (!existing_list)
    {
        return new_node;
    }
    else
    {
        /* Append to the end of existing_list */
        ArgumentList *temp = existing_list;
        while (temp->next)
        {
            temp = temp->next;
        }
        temp->next = new_node;
        return existing_list;
    }
}

ASTNode *create_print_statement_node(ASTNode *expr)
{
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = NODE_PRINT_STATEMENT;
    node->data.op.left = expr;
    return node;
}

ASTNode *create_error_statement_node(ASTNode *expr)
{
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = NODE_ERROR_STATEMENT;
    node->data.op.left = expr;
    return node;
}

ASTNode *create_statement_list(ASTNode *statement, ASTNode *existing_list)
{
    if (!existing_list)
    {
        // If there's no existing list, create a new one
        ASTNode *node = malloc(sizeof(ASTNode));
        node->type = NODE_STATEMENT_LIST;
        node->data.statements = malloc(sizeof(StatementList));
        node->data.statements->statement = statement;
        node->data.statements->next = NULL;
        return node;
    }
    else
    {
        // Append at the end of existing_list
        StatementList *sl = existing_list->data.statements;
        while (sl->next)
        {
            sl = sl->next;
        }
        // Now sl is the last element; append the new statement
        StatementList *new_item = malloc(sizeof(StatementList));
        new_item->statement = statement;
        new_item->next = NULL;
        sl->next = new_item;
        return existing_list;
    }
}

bool is_const_variable(const char *name)
{
    for (int i = 0; i < var_count; i++)
    {
        if (strcmp(symbol_table[i].name, name) == 0)
        {
            return symbol_table[i].modifiers.is_const;
        }
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
    if (!node)
        return false;

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
        if (!check_and_mark_identifier(node, "Undefined variable in type check"))
            exit(1);
        for (int i = 0; i < var_count; i++)
        {
            if (strcmp(symbol_table[i].name, node->data.name) == 0)
            {
                return symbol_table[i].var_type == VAR_SHORT;
            }
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
    default:
        return false;
    }
}

bool is_float_expression(ASTNode *node)
{
    if (!node)
        return false;

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
        if (!check_and_mark_identifier(node, "Undefined variable in type check"))
            exit(1);
        for (int i = 0; i < var_count; i++)
        {
            if (strcmp(symbol_table[i].name, node->data.name) == 0)
            {
                return symbol_table[i].var_type == VAR_FLOAT;
            }
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
    default:
        return false;
    }
}

bool is_double_expression(ASTNode *node)
{
    if (!node)
        return false;

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
        if (!check_and_mark_identifier(node, "Undefined variable in type check"))
            exit(1);
        for (int i = 0; i < var_count; i++)
        {
            if (strcmp(symbol_table[i].name, node->data.name) == 0)
            {
                return symbol_table[i].var_type == VAR_DOUBLE;
            }
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
    default:
        return false;
    }
}

int evaluate_expression(ASTNode *node)
{
    if (is_short_expression(node))
    {
        return (short)evaluate_expression_short(node);
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

    char *name = node->data.op.left->data.name;
    check_const_assignment(name);

    ASTNode *value_node = node->data.op.right;
    TypeModifiers mods = node->modifiers;

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
    if (!node)
        return;
    switch (node->type)
    {
    case NODE_ASSIGNMENT:
    {
        char *name = node->data.op.left->data.name;
        check_const_assignment(name);

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
        else if (is_float_expression(value_node))
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
        if (evaluate_expression(node->data.if_stmt.condition))
        {
            execute_statement(node->data.if_stmt.then_branch);
        }
        else if (node->data.if_stmt.else_branch)
        {
            execute_statement(node->data.if_stmt.else_branch);
        }
        break;
    case NODE_SWITCH_STATEMENT:
        execute_switch_statement(node);
        break;
    case NODE_BREAK_STATEMENT:
        // Signal to break out of the current loop/switch
        longjmp(break_env, 1);
        break;
    default:
        yyerror("Unknown statement type");
        break;
    }
}

void execute_statements(ASTNode *node)
{
    if (!node)
        return;
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
    if (setjmp(break_env) == 0)
    {
        // Execute initialization once
        if (node->data.for_stmt.init)
        {
            execute_statement(node->data.for_stmt.init);
        }

        while (1)
        {
            // Evaluate condition
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
        }
    }
}

void execute_while_statement(ASTNode *node)
{
    if (setjmp(break_env) == 0)
    {
        while (evaluate_expression(node->data.while_stmt.cond))
        {
            execute_statement(node->data.while_stmt.body);
        }
    }
}

void execute_do_while_statement(ASTNode *node)
{
    do
    {
        execute_statement(node->data.while_stmt.body);
    } while (evaluate_expression(node->data.while_stmt.cond));
}

ASTNode *create_if_statement_node(ASTNode *condition, ASTNode *then_branch, ASTNode *else_branch)
{
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = NODE_IF_STATEMENT;
    node->data.if_stmt.condition = condition;
    node->data.if_stmt.then_branch = then_branch;
    node->data.if_stmt.else_branch = else_branch;
    return node;
}

ASTNode *create_string_literal_node(char *string)
{
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = NODE_STRING_LITERAL;
    node->data.name = string;
    return node;
}

ASTNode *create_switch_statement_node(ASTNode *expression, CaseNode *cases)
{
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = NODE_SWITCH_STATEMENT;
    node->data.switch_stmt.expression = expression;
    node->data.switch_stmt.cases = cases;
    return node;
}

CaseNode *create_case_node(ASTNode *value, ASTNode *statements)
{
    CaseNode *node = malloc(sizeof(CaseNode));
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
    if (!list)
        return case_node;
    CaseNode *current = list;
    while (current->next)
        current = current->next;
    current->next = case_node;
    return list;
}

ASTNode *create_break_node()
{
    ASTNode *node = malloc(sizeof(ASTNode));
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

void execute_baka_call(ArgumentList *args)
{
    if (!args)
    {
        baka("\n");
        return;
    }
    // parse the first argument as a format string
    // parse subsequent arguments as integers, etc.
    // call "baka(formatString, val, ...)"
}

void execute_ragequit_call(ArgumentList *args)
{
    if (!args)
    {
        yyerror("No arguments provided for ragequit function call");
        exit(EXIT_FAILURE);
    }

    ASTNode *formatNode = args->expr;
    if (formatNode->type != NODE_INT)
    {
        yyerror("First argument to ragequit must be a integer");
        exit(EXIT_FAILURE);
    }

    ragequit(formatNode->data.ivalue);
}

void execute_chill_call(ArgumentList *args)
{
    if (!args)
    {
        yyerror("No arguments provided for chill function call");
        exit(EXIT_FAILURE);
    }

    ASTNode *formatNode = args->expr;
    if (formatNode->type != NODE_INT && !formatNode->modifiers.is_unsigned)
    {
        yyerror("First argument to chill must be a unsigned integer");
        exit(EXIT_FAILURE);
    }

    chill(formatNode->data.ivalue);
}
