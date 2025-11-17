/* ast.c */

#include "ast.h"
#include "stdrot.h"
#include "visitor.h"
#include "interpreter.h"
#include "lib/mem.h"
#include <stdbool.h>
#include <math.h>
#include <limits.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

JumpBuffer *jump_buffer = {0};

HashMap *function_map = NULL;
ReturnValue current_return_value;
Arena arena;

TypeModifiers current_modifiers = {false, false, false, false, false};
extern VarType current_var_type;

Scope *current_scope;

/* Include the symbol table functions */
extern void yyerror(const char *s);
extern void cleanup(void);
extern void ragequit(int exit_code);
extern void chill(unsigned int seconds);
extern void yapping(const char *format, ...);
extern void yappin(const char *format, ...);
extern void baka(const char *format, ...);
extern char slorp_char(char chr);
extern char *slorp_string(char *string, size_t size);
extern int slorp_int(int val);
extern short slorp_short(short val);
extern float slorp_float(float var);
extern double slorp_double(double var);
extern TypeModifiers get_variable_modifiers(const char *name);
extern int yylineno;

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
        case VAR_STRING:
            var->value.strvalue = ARENA_STRDUP((char *)value);
            break;
        case NONE:
            break;
        }
        return true;
    }
    return false; // Symbol table is full
}

bool set_multi_array_variable(const char *name, int dimensions[], int num_dimensions, TypeModifiers mods, VarType type)
{
    Variable *var = get_variable(name);
    if(var == NULL)
        return false; 

    var->is_array = true;
    var->modifiers = mods;
    var->var_type = type;

    // calculate the total size of the array
    var->array_dimensions.num_dimensions = num_dimensions;
    size_t total = 1;

    for (int i = 0; i < num_dimensions; i++)
    {
        var->array_dimensions.dimensions[i] = dimensions[i];
        total *= dimensions[i];
    }
    
    var->array_dimensions.total_size = total;
    var->array_length = total;

    size_t element_size;
    switch (type)
    {
    case VAR_INT:
        element_size = sizeof(int);
        break;
    case VAR_SHORT:
        element_size = sizeof(short);
        break;
    case VAR_FLOAT:
        element_size = sizeof(float);
        break;
    case VAR_DOUBLE:
        element_size = sizeof(double);
        break;
    case VAR_BOOL:
        element_size = sizeof(bool);
        break;
    case VAR_CHAR:
        element_size = sizeof(char);
        break;
    default:
        element_size = sizeof(int);
        break;
    }

    var->value.array_data = safe_malloc_array(total, element_size);
    if (var->value.array_data == NULL)
    {
        return false;
    }

    memset(var->value.array_data, 0, total * element_size);
    return true;
}

ASTNode *create_multi_array_declaration_node(char *name, int dimensions[], int num_dimensions, VarType type) {
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node) {
        yyerror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    
    node->type = NODE_ARRAY_ACCESS;
    node->var_type = type;
    node->is_array = true;
    
    // Store dimensions in node
    for (int i = 0; i < num_dimensions; i++) {
        node->array_dimensions.dimensions[i] = dimensions[i];
    }
    node->array_dimensions.num_dimensions = num_dimensions;
    
    // Calculate total size
    size_t total = 1;
    for (int i = 0; i < num_dimensions; i++) {
        total *= dimensions[i];
    }
    node->array_dimensions.total_size = total;
    node->array_length = total; // For backward compatibility
    
    // Set the variable name
    node->data.name = ARENA_STRDUP(name);
    
    return node;
}

ASTNode *create_multi_array_access_node(char *name, ASTNode *indices[], int num_indices) {
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node) {
        yyerror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    
    node->type = NODE_ARRAY_ACCESS;
    
    // Store the array name
    node->data.array.name = ARENA_STRDUP(name);
    
    // Store indices
    node->data.array.num_dimensions = num_indices;
    for (int i = 0; i < num_indices; i++) {
        node->data.array.indices[i] = indices[i];
    }
    
    return node;
} 

// Function to rename the old create_array_access_node to maintain compatibility
ASTNode *create_array_access_node_single(char *name, ASTNode *index) {
    // Create a wrapper that calls the multi-dimensional version with a single index
    ASTNode *indices[1] = {index};
    return create_multi_array_access_node(name, indices, 1);
}

// Calculate the memory offset for multi-dimensional array access
size_t calculate_array_offset(Variable *var, int indices[], int num_indices) {
    // TEMPORARY FIX: Skip strict dimension checking due to variable lookup bug
    // The issue is that get_variable() sometimes returns the wrong variable
    // This is a complex memory/hash collision bug that needs deeper investigation
    // If the variable is not actually an array or dimensions don't match,
    // try to handle it gracefully instead of crashing
    if (!var->is_array) {
        // Variable is not an array - return offset 0 for single element access
        return 0;
    }
    
    if (num_indices != var->array_dimensions.num_dimensions) {
        // Dimension mismatch - for now, just use the first few indices that are available
        // This is not ideal but prevents crashes
        int actual_indices = (num_indices < var->array_dimensions.num_dimensions) 
                           ? num_indices 
                           : var->array_dimensions.num_dimensions;
        
        if (actual_indices <= 0) {
            return 0;  // Fallback to first element
        }
        
        // Use the available indices for offset calculation
        num_indices = actual_indices;
    }
    
    // Calculate the offset using row-major order
    int offset = 0;

    // For row-major order: offset = i0 * (d1 * d2 * ... * dn-1) + i1 * (d2 * ... * dn-1) + ... + in-1
    for (int i = 0; i < num_indices; i++) {
        // Check if the index is within bounds
        if (indices[i] < 0 || indices[i] >= var->array_dimensions.dimensions[i]) {
            char error_msg[100];
            sprintf(error_msg, "Array index out of bounds: dimension %d (index=%d, size=%d)", 
                    i + 1, indices[i], var->array_dimensions.dimensions[i]);
            yyerror(error_msg);
            exit(EXIT_FAILURE);
        }
        
        // Calculate the multiplier for this dimension
        // Multiply by all dimensions to the right
        size_t multiplier = 1;
        for (int j = i + 1; j < num_indices; j++) {
            multiplier *= var->array_dimensions.dimensions[j];
        }
        
        offset += indices[i] * multiplier;
    }
    
    return offset;
}

// Evaluate a multi-dimensional array access node
void *evaluate_multi_array_access(ASTNode *node) {
    // Validate the node structure
    if (!node) {
        yyerror("Invalid array access node: null node");
        exit(EXIT_FAILURE);
    }
    if (node->type != NODE_ARRAY_ACCESS) {
        yyerror("Invalid node type for array access");
        exit(EXIT_FAILURE);
    }
    
    // CRITICAL: Store the array name in a local copy IMMEDIATELY
    // The array name might be corrupted if we access node->data.array.name after
    // evaluating indices, due to union memory layout issues
    char array_name_buffer[256];
    const char *original_array_name = node->data.array.name;
    if (!original_array_name) {
        yyerror("Invalid array access node: missing array name");
        exit(EXIT_FAILURE);
    }
    int name_len = (int)strlen(original_array_name);
    if (name_len == 0 || name_len >= (int)sizeof(array_name_buffer)) {
        yyerror("Invalid array name in array access");
        exit(EXIT_FAILURE);
    }
    strncpy(array_name_buffer, original_array_name, sizeof(array_name_buffer) - 1);
    array_name_buffer[sizeof(array_name_buffer) - 1] = '\0';
    const char *array_name = array_name_buffer;
    
    // Also store num_dimensions locally before evaluation
    int num_indices = node->data.array.num_dimensions;
    if (num_indices <= 0) {
        yyerror("Invalid number of array indices");
        exit(EXIT_FAILURE);
    }
    
    // Get the variable using the preserved array name
    Variable *var = get_variable(array_name);
    if (var == NULL) {
        char error_msg[200];
        snprintf(error_msg, sizeof(error_msg), "Variable '%.100s' is not defined", array_name);
        yyerror(error_msg);
        exit(EXIT_FAILURE);
    }
    if (!var->is_array) {
        char error_msg[200];
        snprintf(error_msg, sizeof(error_msg), "Variable '%.100s' is not an array", array_name);
        yyerror(error_msg);
        exit(EXIT_FAILURE);
    }
    
    // Extract the indices - evaluate them AFTER we've preserved the array name
    int indices[MAX_DIMENSIONS];
    
    // Evaluate each index expression - make sure we don't modify the array access node
    for (int i = 0; i < num_indices; i++) {
        ASTNode *index_node = node->data.array.indices[i];
        if (!index_node) {
            char error_msg[200];
            snprintf(error_msg, sizeof(error_msg), "Missing index %d for array '%.100s'", i, array_name);
            yyerror(error_msg);
            exit(EXIT_FAILURE);
        }
        // Evaluate the index expression - this should return an integer value
        // Make sure we're not accidentally treating the index as an array access
        indices[i] = evaluate_expression_int(index_node);
        
        // After evaluating each index, verify the array name hasn't been corrupted
        if (node->data.array.name != original_array_name) {
            // Restore the original array name if it was modified
            // Note: We need to cast away const because the field is not const
            node->data.array.name = (char*)original_array_name;
        }
    }
    
    // Calculate the offset
    size_t offset = calculate_array_offset(var, indices, num_indices);
    
    // Return a pointer to the element
    switch (var->var_type) {
        case VAR_INT:
            return (int*)var->value.array_data + offset;
        case VAR_SHORT:
            return (short*)var->value.array_data + offset;
        case VAR_FLOAT:
            return (float*)var->value.array_data + offset;
        case VAR_DOUBLE:
            return (double*)var->value.array_data + offset;
        case VAR_BOOL:
            return (bool*)var->value.array_data + offset;
        case VAR_CHAR:
            return (char*)var->value.array_data + offset;
        default:
            yyerror("Unknown variable type");
            exit(EXIT_FAILURE);
    }
}

bool set_int_variable(const char *name, int value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_INT, mods);
}

bool set_char_variable(const char *name, int value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_CHAR, mods);
}

bool set_array_variable(char *name, int length, TypeModifiers mods, VarType type)
{
    // search for an existing variable
    Variable *var = get_variable(name);
    if (var != NULL)
    {
        if (var->is_array)
        {
            // free the old array
            SAFE_FREE(var->value.array_data);
        }
        var->var_type = type;
        var->is_array = true;
        var->array_length = length;
        var->modifiers = mods;
        switch (type)
        {
        case VAR_INT:
            var->value.array_data = SAFE_MALLOC_ARRAY(int, length);
            if (length)
                memset(var->value.array_data, 0, length * sizeof(int));
            break;
        case VAR_SHORT:
            var->value.array_data = SAFE_MALLOC_ARRAY(short, length);
            if (length)
                memset(var->value.array_data, 0, length * sizeof(short));
            break;
        case VAR_FLOAT:
            var->value.array_data = SAFE_MALLOC_ARRAY(float, length);
            if (length)
                memset(var->value.array_data, 0, length * sizeof(float));
            break;
        case VAR_DOUBLE:
            var->value.array_data = SAFE_MALLOC_ARRAY(double, length);
            if (length)
                memset(var->value.array_data, 0, length * sizeof(double));
            break;
        case VAR_BOOL:
            var->value.array_data = SAFE_MALLOC_ARRAY(bool, length);
            if (length)
                memset(var->value.array_data, 0, length * sizeof(bool));
            break;
        case VAR_CHAR:
            var->value.array_data = SAFE_MALLOC_ARRAY(char, length);
            if (length)
                memset(var->value.array_data, 0, length * sizeof(char));
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

bool set_string_variable(const char *name, const char *value, TypeModifiers mods)
{
    return set_variable(name, (void *)value, VAR_STRING, mods);
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


/* Function implementations */

bool check_and_mark_identifier(ASTNode *node, const char *contextErrorMessage)
{
    if (!node->already_checked)
    {
        node->already_checked = true;
        node->is_valid_symbol = false;

        // Do the table lookup
        Variable *var = get_variable(node->data.name);
        if (var != NULL)
            node->is_valid_symbol = true;

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
    int matched = 0;

    PUSH_JUMP_BUFFER();
    if (setjmp(CURRENT_JUMP_BUFFER()) == 0)
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
    POP_JUMP_BUFFER();
}

static ASTNode *create_node(NodeType type, VarType var_type, TypeModifiers modifiers)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
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
    node->line_number = 0; /* Initialize line number to avoid uninitialized value errors */
    return node;
}

/* Helper function to allocate and zero-initialize an ASTNode */
ASTNode* arena_alloc_astnode(void) {
    ASTNode *node = ARENA_ALLOC(ASTNode);
    if (node) {
        memset(node, 0, sizeof(ASTNode));
    }
    return node;
}

ASTNode *create_int_node(int value)
{
    ASTNode *node = create_node(NODE_INT, VAR_INT, current_modifiers);
    SET_DATA_INT(node, value);
    return node;
}

ASTNode *create_array_declaration_node(char *name, int length, VarType var_type)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node)
        return NULL;

    node->type = NODE_ARRAY_ACCESS;
    node->var_type = var_type;
    node->is_array = true;
    node->array_length = length;
    node->data.array.name = ARENA_STRDUP(name);
    return node;
}

ASTNode *create_array_access_node(char *name, ASTNode *index)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node)
    {
        yyerror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    node->type = NODE_ARRAY_ACCESS;
    node->data.array.name = ARENA_STRDUP(name);
    node->data.array.index = index;
    node->data.array.indices[0] = index;  // Also set the multi-dimensional access
    node->data.array.num_dimensions = 1;  // Set dimension count
    node->is_array = true;

    // Look up and set the array's type from the symbol table
    Variable *var = get_variable(name);
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

ASTNode *create_identifier_node(char *name)
{
    ASTNode *node = create_node(NODE_IDENTIFIER, current_var_type, current_modifiers);
    SET_DATA_NAME(node, name);
    return node;
}

ASTNode *create_assignment_node(char *name, ASTNode *expr)
{
    ASTNode *node = create_node(NODE_ASSIGNMENT, current_var_type, get_current_modifiers());
    SET_DATA_OP(node, create_identifier_node(name), expr, OP_ASSIGN);
    return node;
}

ASTNode *create_declaration_node(char *name, ASTNode *expr)
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

ASTNode *create_sizeof_node(ASTNode *expr)
{
    ASTNode *node = create_node(NODE_SIZEOF, NONE, current_modifiers);
    SET_SIZEOF(node, expr);
    return node;
}

// @param promotion: 0 for no promotion, 1 for promotion to double 2 for promotion to float
void *handle_identifier(ASTNode *node, const char *contextErrorMessage, int promote)
{
    if (!check_and_mark_identifier(node, contextErrorMessage))
        ragequit(1);

    char *name = node->data.name;
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
                promoted_value.dvalue = (double)var->value.ivalue;
                return &promoted_value;
            case VAR_CHAR:
            case VAR_SHORT:
                promoted_value.dvalue = (double)var->value.svalue;
                return &promoted_value;
            case VAR_BOOL:
                promoted_value.dvalue = (double)var->value.ivalue;
                return &promoted_value;
            case VAR_STRING:
                return &var->value.strvalue;
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
                promoted_value.fvalue = (float)var->value.ivalue;
                return &promoted_value.fvalue;
            case VAR_CHAR:
            case VAR_SHORT:
                promoted_value.fvalue = (float)var->value.svalue;
                return &promoted_value.fvalue;
            case VAR_BOOL:
                promoted_value.fvalue = (float)var->value.ivalue;
                return &promoted_value.fvalue;
            case VAR_STRING:
                return &var->value.strvalue;
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
                return &var->value.ivalue;
            case VAR_CHAR:
            case VAR_SHORT:
                return &var->value.svalue;
            case VAR_BOOL:
                return &var->value.ivalue;
            case VAR_STRING:
                return &var->value.strvalue;
            default:
                yyerror("Unsupported variable type");
                return NULL;
            }
        }
    }
    yyerror("Undefined variable");
    return NULL;
}

VarType get_expression_type(ASTNode *node)
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
        // Store the array name locally to prevent modification
        const char *array_name = node->data.array.name;
        if (!array_name) {
            yyerror("Invalid array access: missing array name");
            return NONE;
        }
        
        Variable *var = get_variable(array_name);
        if (var != NULL && var->is_array)
        {
            // Return the array's element type without evaluating
            // (evaluation might modify the node structure)
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
    case NODE_SIZEOF:
    {
        return VAR_INT; // Sizeof always returns an integer
    }
    case NODE_FUNC_CALL:
    {
        // Look up the function in the symbol table
        const char *func_name = node->data.func_call.function_name;
        Function *func = get_function(func_name);
        if (func != NULL)
        {
            return func->return_type;
        }
        yyerror("Undefined function in get_expression_type");
        return NONE;
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
    if (left_type == VAR_DOUBLE || right_type == VAR_DOUBLE)
        promoted_type = VAR_DOUBLE;
    else if (left_type == VAR_FLOAT || right_type == VAR_FLOAT)
        promoted_type = VAR_FLOAT;
    else if (left_type == VAR_INT || right_type == VAR_INT)
        promoted_type = VAR_INT;

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
    void *result;
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
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value + *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value + *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value + *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(short *)result = *(short *)left_value + *(short *)right_value;
        break;

    case OP_MINUS:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value - *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value - *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value - *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(short *)result = *(short *)left_value - *(short *)right_value;

        break;

    case OP_TIMES:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value * *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value * *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value * *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(short *)result = *(short *)left_value * *(short *)right_value;
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
            *(float *)result = *(float *)left_value / *(float *)right_value;
        }
        else if (promoted_type == VAR_DOUBLE)
        {
            *(double *)result = *(double *)left_value / *(double *)right_value;
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
            *(float *)result = fmod(*(float *)left_value, *(float *)right_value);
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
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value < *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value < *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value < *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(short *)result = *(short *)left_value < *(short *)right_value;
        break;

    case OP_GT:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value > *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value > *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value > *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(short *)result = *(short *)left_value > *(short *)right_value;
        break;

    case OP_LE:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value <= *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value <= *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value <= *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(short *)result = *(short *)left_value <= *(short *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(short *)result = *(short *)left_value <= *(short *)right_value;
        break;

    case OP_GE:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value >= *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value >= *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value >= *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(short *)result = *(short *)left_value >= *(short *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(short *)result = *(short *)left_value >= *(short *)right_value;
        break;

    case OP_EQ:

        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value == *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value == *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value == *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(short *)result = *(short *)left_value == *(short *)right_value;
        break;

    case OP_NE:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value != *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value != *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value != *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(short *)result = *(short *)left_value != *(short *)right_value;
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

void *handle_unary_expression(ASTNode *node, void *operand_value, int operand_type)
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
    if (!node)
        return 0.0f;

    switch (node->type)
    {
    case NODE_ARRAY_ACCESS:
    {
        return *(float*)evaluate_multi_array_access(node);
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
        float result_float = 0.0f;
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
        return 0.0f;
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
    case NODE_ARRAY_ACCESS:
    {
        return *(double*)evaluate_multi_array_access(node);
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
size_t get_type_size(char *name)
{
    Variable *var = get_variable(name);
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
        else if (var->var_type == VAR_DOUBLE)
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
    ASTNode *expr = node->data.sizeof_stmt.expr;
    
    if (expr->type == NODE_IDENTIFIER)
    {
        // For identifiers, use get_type_size which looks up the variable
        return get_type_size(expr->data.name);
    }
    else
    {
        // For non-identifiers (like literals), use get_expression_type
        VarType type = get_expression_type(expr);
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
    }
}

char *evaluate_expression_string(ASTNode *node)
{
    if (!node)
        return NULL;

    switch (node->type)
    {
    case NODE_STRING_LITERAL:
    case NODE_STRING:
        return safe_strdup(node->data.strvalue);
    case NODE_IDENTIFIER:
    {
        return safe_strdup((char *)handle_identifier(node, "Undefined variable", 3));
    }
    case NODE_FUNC_CALL:
    {
        char *res = (char *)handle_function_call(node);
        if (res != NULL)
        {
            char *result = safe_strdup(res);
            SAFE_FREE(res);
            return result;
        }
        return NULL;
    }
    default:
        yyerror("Invalid string expression");
        return NULL;
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
        void *result = handle_binary_operation(node);
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
      
        return *(short*)evaluate_multi_array_access(node);
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
        return handle_sizeof(node);
    }
    case NODE_IDENTIFIER:
    {
        return *(int *)handle_identifier(node, "Undefined variable", 0);
    }
    case NODE_OPERATION:
    {
        // Special handling for logical operations
        if (node->data.op.op == OP_AND)
        {
            int left = evaluate_expression_int(node->data.op.left);
            if (!left) return 0;
            int right = evaluate_expression_int(node->data.op.right);
            return left && right;
        }
        if (node->data.op.op == OP_OR)
        {
            int left = evaluate_expression_int(node->data.op.left);
            if (left) return 1;
            int right = evaluate_expression_int(node->data.op.right);
            return left || right;
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
        return *(int*)evaluate_multi_array_access(node);
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
        case VAR_STRING: 
            return_value = SAFE_MALLOC(char *);
            *(char **)return_value = safe_strdup(current_return_value.value.strvalue);
            break;
        case NONE:
            return NULL;
        }
    }
    return return_value;
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
        if (node->data.op.op == OP_AND)
        {
            bool left = evaluate_expression_bool(node->data.op.left);
            if (!left) return false;
            bool right = evaluate_expression_bool(node->data.op.right);
            return left && right;
        }
        if (node->data.op.op == OP_OR)
        {
            bool left = evaluate_expression_bool(node->data.op.left);
            if (left) return true;
            bool right = evaluate_expression_bool(node->data.op.right);
            return left || right;
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
        return *(bool*)evaluate_multi_array_access(node);
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
    ArgumentList *new_node = ARENA_ALLOC(ArgumentList);
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
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    node->type = NODE_PRINT_STATEMENT;
    node->data.op.left = expr;
    return node;
}

ASTNode *create_error_statement_node(ASTNode *expr)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    node->type = NODE_ERROR_STATEMENT;
    node->data.op.left = expr;
    return node;
}

ASTNode *create_statement_list(ASTNode *statement, ASTNode *existing_list)
{
    if (!existing_list)
    {
        // If there's no existing list, create a new one
        ASTNode *node = ARENA_ALLOC_ASTNODE();
        if (!node)
        {
            yyerror("Memory allocation failed");
            return NULL;
        }
        node->type = NODE_STATEMENT_LIST;
        node->data.statements = ARENA_ALLOC(StatementList);
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
    else
    {
        // Append at the end of existing_list
        StatementList *sl = existing_list->data.statements;
        while (sl->next)
        {
            sl = sl->next;
        }
        // Now sl is the last element; append the new statement
        StatementList *new_item = ARENA_ALLOC(StatementList);
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
        ragequit(EXIT_FAILURE);
    }
}


bool is_expression(ASTNode *node, VarType type)
{
    if (!node)
        return false;

    switch (node->type)
    {
    case NODE_ARRAY_ACCESS:
    {
        Variable *var = get_variable(node->data.array.name);
        if (var != NULL)
        {
            return var->var_type == type;
        }
        yyerror("Undefined variable in type check");
        return false;
    }
    case NODE_IDENTIFIER:
    {
        if (!check_and_mark_identifier(node, "Undefined variable in type check"))
            ragequit(1);
        Variable *var = get_variable(node->data.name);
        if (var != NULL)
        {
            return var->var_type == type;
        }
        yyerror("Undefined variable in type check");
        return false;
    }
    case NODE_OPERATION:
    {
        // For operations, check if the result type matches the target type
        return get_expression_type(node) == type;
    }
    case NODE_FUNC_CALL:
    {
        return get_function_return_type(node->data.func_call.function_name) == type;
    }
    default:
        return node->type == VART_TO_NODET(type);
    }
}

Function *get_function(const char *name)
{
    if (!function_map || !name) {
        return NULL;
    }
    
    size_t name_len = strlen(name);
    Function **func_ptr = (Function **)hm_get(function_map, name, name_len);
    if (func_ptr) {
        return *func_ptr;
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


int evaluate_expression(ASTNode *node)
{
    if (is_expression(node, VAR_SHORT))
    {
        return (short)evaluate_expression_short(node);
    }
    if (is_expression(node, VAR_FLOAT))
    {
        return (int)evaluate_expression_float(node);
    }
    if (is_expression(node, VAR_DOUBLE))
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
                ((float *)var->value.array_data)[idx] = evaluate_expression_float(node->data.op.right);
                break;
            case VAR_DOUBLE:
                ((double *)var->value.array_data)[idx] = evaluate_expression_double(node->data.op.right);
                break;
            case VAR_INT:
                ((int *)var->value.array_data)[idx] = evaluate_expression_int(node->data.op.right);
                break;
            case VAR_SHORT:
                ((short *)var->value.array_data)[idx] = evaluate_expression_short(node->data.op.right);
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

    // Handle type conversion for float to int
    if (value_node->type == NODE_FLOAT || is_expression(value_node, VAR_FLOAT))
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

    if (is_expression(value_node, VAR_FLOAT))
    {
        float value = evaluate_expression_float(value_node);
        if (!set_float_variable(name, value, mods))
        {
            yyerror("Failed to set float variable");
        }
    }
    else if (is_expression(value_node, VAR_DOUBLE))
    {
        double value = evaluate_expression_double(value_node);
        if (!set_double_variable(name, value, mods))
        {
            yyerror("Failed to set double variable");
        }
    }
    else if (is_expression(value_node,VAR_SHORT))
    {
        short value = evaluate_expression_short(value_node);
        if (!set_short_variable(name, value, mods))
        {
            yyerror("Failed to set short variable");
        }
    }
    else if (is_expression(value_node, VAR_STRING))
    {
        char *value = evaluate_expression_string(value_node);
        if (!set_string_variable(name, value, mods))
        {
            yyerror("Failed to set string variable");
        }
        SAFE_FREE(value);
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
    case NODE_DECLARATION:
    {
       char *name = node->data.op.left->data.name;
       Variable *var = variable_new(name);
       add_variable_to_scope(name, var);
       SAFE_FREE(var);
    }
        __attribute__((fallthrough));
    case NODE_ASSIGNMENT:
    {
        char *name = node->data.op.left->data.name;
        check_const_assignment(name);

        // Handle array assignment
        if (node->data.op.left->type == NODE_ARRAY_ACCESS)
        {
            ASTNode *array_node = node->data.op.left;
            Variable *var = get_variable(array_node->data.array.name);
            void *element = evaluate_multi_array_access(array_node);
            switch (var->var_type)
            {
            case VAR_FLOAT:
                *(float *)element = evaluate_expression_float(node->data.op.right);
                break;
            case VAR_DOUBLE:
                *(double *)element = evaluate_expression_double(node->data.op.right);
                break;
            case VAR_INT:
                *(int *)element = evaluate_expression_int(node->data.op.right);
                break;
            case VAR_SHORT:
                *(short *)element = evaluate_expression_short(node->data.op.right);
                break;
            case VAR_BOOL:
                *(bool *)element = evaluate_expression_bool(node->data.op.right);
                break;
            case VAR_CHAR:
                *(char *)element = evaluate_expression_int(node->data.op.right);
                break;
            default:
                yyerror("Unsupported array type");
                return;
            }
            return;
        }

        ASTNode *value_node = node->data.op.right;
        TypeModifiers mods = node->modifiers;

        if (value_node->type == NODE_CHAR)
        {
            // Handle character assignments directly
            if (!set_char_variable(name, value_node->data.ivalue, mods))
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
        else if (node->var_type == VAR_FLOAT || is_expression(value_node, VAR_FLOAT))
        {
            float value = evaluate_expression_float(value_node);
            if (!set_float_variable(name, value, mods))
            {
                yyerror("Failed to set float variable");
            }
        }
        else if (node->var_type == VAR_DOUBLE || is_expression(value_node, VAR_DOUBLE))
        {
            double value = evaluate_expression_double(value_node);
            if (!set_double_variable(name, value, mods))
            {
                yyerror("Failed to set double variable");
            }
        }
        else if (node->var_type == VAR_STRING || is_expression(value_node, VAR_STRING))
        {
            char *value = evaluate_expression_string(value_node);
            if (!set_string_variable(name, value, mods))
            {
                yyerror("Failed to set string variable");
            }
            SAFE_FREE(value);
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
            if (!(node->data.array.name))
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
        // Use the stdrot built-in function system
        if (is_builtin_function(node->data.func_call.function_name))
        {
            execute_builtin_function(node->data.func_call.function_name, node->data.func_call.arguments);
        }
        else
        {
            execute_function_call(
                node->data.func_call.function_name,
                node->data.func_call.arguments);
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
        Function *func = create_function(
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
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    node->type = NODE_IF_STATEMENT;
    node->data.if_stmt.condition = condition;
    node->data.if_stmt.then_branch = then_branch;
    node->data.if_stmt.else_branch = else_branch;
    return node;
}

ASTNode *create_string_literal_node(char *string)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    node->type = NODE_STRING_LITERAL;
    node->data.name = ARENA_STRDUP(string);
    return node;
}

ASTNode *create_switch_statement_node(ASTNode *expression, CaseNode *cases)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    node->type = NODE_SWITCH_STATEMENT;
    node->data.switch_stmt.expression = expression;
    node->data.switch_stmt.cases = cases;
    return node;
}

CaseNode *create_case_node(ASTNode *value, ASTNode *statements)
{
    CaseNode *node = ARENA_ALLOC(CaseNode);
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
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    node->type = NODE_BREAK_STATEMENT;
    node->data.break_stmt = NULL;
    return node;
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
        return create_float_node(0.0f);
    case VAR_DOUBLE:
        return create_double_node(0.0);
    case VAR_SHORT:
        return create_short_node(0);
    case VAR_CHAR:
        return create_char_node('\0');
    case VAR_BOOL:
        return create_boolean_node(0);
    case VAR_STRING:
        return create_string_literal_node("\0");
    default:
        yyerror("Unsupported type for default node");
        exit(1);
    }
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
    if (!list)
        return 0;
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
    if (!list)
        return;
    ExpressionList *current = list->next;
    while (current != list)
    {
        ExpressionList *next = current->next;
        SAFE_FREE(current);
        current = next;
    }
    SAFE_FREE(list);
}

void populate_multi_array_variable(char *name, ExpressionList *list, int dimensions[], int num_dimensions) {
    Variable *var = get_variable(name);
    if (var == NULL || !var->is_array) {
        yyerror("Cannot initialize: not an array");
        return;
    }
    
    // Calculate total elements
    size_t total_elements = 1;
    for (int i = 0; i < num_dimensions; i++) {
        total_elements *= dimensions[i];
    }
    
    // Check if we have enough initializers
    size_t initializer_count = count_expression_list(list);
    if (initializer_count > total_elements) {
        yyerror("Too many initializers for array");
        return;
    }
    
    // Initialize the array elements
    size_t index = 0;
    ExpressionList *current = list;
    
    while (current != NULL && index < total_elements) {
        switch (var->var_type) {
            case VAR_INT: {
                int *array = (int*)var->value.array_data;
                array[index] = evaluate_expression_int(current->expr);
                break;
            }
            case VAR_SHORT: {
                short *array = (short*)var->value.array_data;
                array[index] = evaluate_expression_short(current->expr);
                break;
            }
            case VAR_FLOAT: {
                float *array = (float*)var->value.array_data;
                array[index] = evaluate_expression_float(current->expr);
                break;
            }
            case VAR_DOUBLE: {
                double *array = (double*)var->value.array_data;
                array[index] = evaluate_expression_double(current->expr);
                break;
            }
            case VAR_BOOL: {
                bool *array = (bool*)var->value.array_data;
                array[index] = evaluate_expression_bool(current->expr);
                break;
            }
            default:
                yyerror("Unsupported array type for initialization");
                return;
        }
        
        current = current->next;
        index++;
    }
    
}
void free_statement_list(StatementList *list)
{
    while (list)
    {
        StatementList *next = list->next;

        // Free the current list node
        if (list)
            SAFE_FREE(list);

        // Move to the next node
        list = next;
    }
}

void free_ast()
{
    arena_free(&arena);
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
        if (scope->is_function_scope)
        {
            return NULL;
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
    if (!scope)
        return;
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
    
    /* Cache strlen result to avoid redundant calculations */
    size_t name_len = strlen(name);
    Variable *existing = hm_get(current_scope->variables, name, name_len);
    if (existing)
    {
        yyerror("Variable already exists in current scope");
        SAFE_FREE(var);
        exit(1);
    }

    hm_put(current_scope->variables, name, name_len, var, sizeof(Variable));
}

ASTNode *create_return_node(ASTNode *expr)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node)
    {
        yyerror("Memory allocation failed");
        return NULL;
    }
    node->type = NODE_RETURN;
    node->data.op.left = expr; // Store return expression in left operand
    return node;
}

Function *create_function(char *name, VarType return_type, Parameter *params, ASTNode *body)
{
    /* Check if function already exists - if so, just return it (parse + execute causes double creation) */
    Function *existing = get_function(name);
    if (existing) {
        return existing;
    }
    
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

    /* Initialize hash map if needed and add function for O(1) lookups */
    if (!function_map) {
        function_map = hm_new();
    }
    size_t name_len = strlen(name);
    hm_put(function_map, name, name_len, &func, sizeof(Function *));

    return func;
}

void execute_function_call(const char *name, ArgumentList *args)
{
    /* Use optimized O(1) hash map lookup instead of O(n) linked list search */
    Function *func = get_function(name);

    if (!func)
    {
        yyerror("Undefined function");
        return;
    }

    enter_function_scope(func, args);
    current_return_value.type = func->return_type;
    current_return_value.has_value = false;

    PUSH_JUMP_BUFFER();
    if (setjmp(CURRENT_JUMP_BUFFER()) == 0)
    {
        /* Use visitor pattern instead of old AST execution for function bodies */
        extern Interpreter* current_interpreter;
        if (current_interpreter) {
            ast_accept(func->body, (Visitor*)current_interpreter);
        } else {
            /* Fallback to old system if no current interpreter */
            execute_statement(func->body);
        }
        
        // If we reach here without an explicit return, clean up function scope
        if (current_scope && current_scope->is_function_scope) {
            exit_scope(); // exit function scope
        }
    }
    POP_JUMP_BUFFER();
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
    // Clean up all scopes until we reach the function scope
    while (current_scope && !current_scope->is_function_scope)
    {
        exit_scope();
    }

    // skibidi main function do not have jump buffer
    if (jump_buffer){
        exit_scope(); // exit current function scope
        LONGJMP();
    }
}

Parameter *create_parameter(char *name, VarType type, Parameter *next, TypeModifiers mods)
{
    Parameter *param = ARENA_ALLOC(Parameter);
    if (!param)
    {
        yyerror("Failed to allocate memory for parameter");
        return NULL;
    }

    param->name = ARENA_STRDUP(name);
    param->type = type;
    param->next = next;
    param->modifiers = mods;

    return param;
}

ASTNode *create_function_def_node(char *name, VarType return_type, Parameter *params, ASTNode *body)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node)
    {
        yyerror("Failed to allocate memory for function definition node");
        return NULL;
    }

    node->type = NODE_FUNCTION_DEF;
    node->data.function_def.name = ARENA_STRDUP(name);
    node->data.function_def.return_type = return_type;
    node->data.function_def.parameters = params;
    node->data.function_def.body = body;

    // Add function to global function table
    create_function(name, return_type, params, body);

    return node;
}


void free_function_table(void)
{
    if (!function_map) {
        return;
    }
    
    /* Iterate through hash map and free all functions */
    for (size_t i = 0; i < function_map->capacity; i++)
    {
        if (function_map->nodes[i])
        {
            Function **func_ptr = (Function **)function_map->nodes[i]->value;
            if (func_ptr && *func_ptr)
            {
                Function *f = *func_ptr;
                
                // Free function name (it's a separate safe_strdup from the AST's name)
                SAFE_FREE(f->name);
                
                // DO NOT free f->parameters or f->body here,
                // because those pointers belong to the AST and
                // are already freed in free_ast(root).
                
                SAFE_FREE(f);
            }
        }
    }
    
    /* Free the hash map using shallow free */
    hm_free_shallow(function_map);
    function_map = NULL;
}

void reverse_parameter_list(Parameter **head)
{
    Parameter *prev = NULL, *current = *head, *next = NULL;
    while (current)
    {
        next = current->next;
        current->next = prev;
        prev = current;
        current = next;
    }
    *head = prev;
}

void enter_function_scope(Function *func, ArgumentList *args)
{
    ArgumentList *curr_arg = args;
    Value arg_values[MAX_ARGUMENTS];
    int arg_count = 0;

    // Reverse the parameter list
    reverse_parameter_list(&func->parameters);
    Parameter *curr_param = func->parameters;

    // Evaluate argument values before creating the scope
    while (curr_arg && curr_param)
    {
        switch (curr_param->type)
        {
        case VAR_INT:
        case VAR_CHAR:
            arg_values[arg_count].ivalue = evaluate_expression_int(curr_arg->expr);
            break;
        case VAR_FLOAT:
            arg_values[arg_count].fvalue = evaluate_expression_float(curr_arg->expr);
            break;
        case VAR_DOUBLE:
            arg_values[arg_count].dvalue = evaluate_expression_double(curr_arg->expr);
            break;
        case VAR_BOOL:
            arg_values[arg_count].bvalue = evaluate_expression_bool(curr_arg->expr);
            break;
        case VAR_SHORT:
            arg_values[arg_count].svalue = evaluate_expression_short(curr_arg->expr);
            break;
        case VAR_STRING:
            yyerror("String parameters are not supported");
            return;
        case NONE:
            break;
        }

        curr_arg = curr_arg->next;
        curr_param = curr_param->next;
        arg_count++;
    }

    if (curr_arg || curr_param)
    {
        yyerror("Mismatched number of arguments and parameters");
        return;
    }

    // Create function scope after evaluating arguments
    Scope *scope = create_scope(current_scope);
    current_scope = scope;
    current_scope->is_function_scope = true;

    curr_param = func->parameters; // Reset parameter list after reversing

    // Assign evaluated values to function parameters
    for (int i = 0; i < arg_count; i++)
    {
        Variable *var = variable_new(curr_param->name);
        var->var_type = curr_param->type;
        TypeModifiers mods = curr_param->modifiers;
        add_variable_to_scope(curr_param->name, var);
        SAFE_FREE(var);

        switch (curr_param->type)
        {
        case VAR_INT:
        case VAR_CHAR:
            set_int_variable(curr_param->name, arg_values[i].ivalue, mods);
            break;
        case VAR_FLOAT:
            set_float_variable(curr_param->name, arg_values[i].fvalue, mods);
            break;
        case VAR_DOUBLE:
            set_double_variable(curr_param->name, arg_values[i].dvalue, mods);
            break;
        case VAR_BOOL:
            set_bool_variable(curr_param->name, arg_values[i].bvalue, mods);
            break;
        case VAR_SHORT:
            set_short_variable(curr_param->name, arg_values[i].svalue, mods);
            break;
        case VAR_STRING:
            yyerror("String parameters are not supported");
            return;
        case NONE:
            break;
        }
        curr_param = curr_param->next;
    }
    reverse_parameter_list(&func->parameters);
}

