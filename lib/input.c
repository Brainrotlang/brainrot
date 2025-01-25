/**
 * input.c - Implementation of input functions
 *
 * This file contains the definitions of the functions declared in input.h.
 */

#include "input.h"

/**
 * Clears the remaining input in stdin to prevent it from affecting subsequent reads.
 */
void clear_stdin_buffer(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
}

/**
 * Safely reads a single character value
 *
 * @param value Pointer to store the character value
 * @return input_status indicating success or type of error
 */
input_status input_char(char *value)
{
    if (value == NULL)
    {
        return INPUT_NULL_PTR;
    }

    char buffer[2]; // One for the character, one for null terminator
    size_t chars_read;

    input_status status = input_string(buffer, sizeof(buffer), &chars_read);
    if (status != INPUT_SUCCESS)
    {
        return status;
    }

    // Check if we got exactly one character
    if (chars_read != 1)
    {
        return INPUT_INVALID_LENGTH;
    }

    *value = buffer[0];
    return INPUT_SUCCESS;
}

/**
 * Safely reads a string with a maximum length
 *
 * @param buffer Pointer to the buffer where the string will be stored
 * @param buffer_size Size of the buffer in bytes
 * @param chars_read Pointer to store the number of characters read
 * @return input_status indicating success or type of error
 */
input_status input_string(char *buffer, size_t buffer_size, size_t *chars_read)
{
    if (buffer == NULL || chars_read == NULL)
    {
        return INPUT_NULL_PTR;
    }

    if (buffer_size == 0)
    {
        return INPUT_BUFFER_OVERFLOW;
    }

    // Clear errno before operation
    errno = 0;

    // Read input ensuring space for null terminator
    if (fgets(buffer, (int)buffer_size, stdin) == NULL)
    {
        if (ferror(stdin))
        {
            clearerr(stdin);
            return INPUT_IO_ERROR;
        }
        // EOF reached
        buffer[0] = '\0';
        *chars_read = 0;
        return INPUT_SUCCESS;
    }

    // Check if the input was truncated (no newline found)
    size_t len = strnlen(buffer, buffer_size);
    if (len == buffer_size - 1 && buffer[len - 1] != '\n')
    {
        // Input was truncated; clear the remaining input
        clear_stdin_buffer();
        return INPUT_BUFFER_OVERFLOW;
    }

    // Remove trailing newline if present
    if (len > 0 && buffer[len - 1] == '\n')
    {
        buffer[len - 1] = '\0';
        len--;
    }

    *chars_read = len;
    return INPUT_SUCCESS;
}

/**
 * Safely reads an integer value
 *
 * @param value Pointer to store the integer value
 * @return input_status indicating success or type of error
 */
input_status input_int(int *value)
{
    if (value == NULL)
    {
        return INPUT_NULL_PTR;
    }

    char buffer[32]; // Large enough for any integer
    size_t chars_read;

    input_status status = input_string(buffer, sizeof(buffer), &chars_read);
    if (status != INPUT_SUCCESS)
    {
        return status;
    }

    // Clear errno before conversion
    errno = 0;
    char *endptr;
    long result = strtol(buffer, &endptr, 10);

    // Check for conversion errors
    if (endptr == buffer || *endptr != '\0')
    {
        return INPUT_CONVERSION_ERROR;
    }

    // Check for overflow/underflow
    if (errno == ERANGE || result > INT_MAX || result < INT_MIN)
    {
        return INPUT_INTEGER_OVERFLOW;
    }

    *value = (int)result;
    return INPUT_SUCCESS;
}

/**
 * Safely reads an short value
 *
 * @param value Pointer to store the short value
 * @return input_status indicating success or type of error
 */
input_status input_short(short *value)
{
    if (value == NULL)
    {
        return INPUT_NULL_PTR;
    }

    char buffer[32]; // Large enough for any integer
    size_t chars_read;

    input_status status = input_string(buffer, sizeof(buffer), &chars_read);
    if (status != INPUT_SUCCESS)
    {
        return status;
    }

    // Clear errno before conversion
    errno = 0;
    char *endptr;
    long result = strtol(buffer, &endptr, 10);

    // Check for conversion errors
    if (endptr == buffer || *endptr != '\0')
    {
        return INPUT_CONVERSION_ERROR;
    }

    // Check for overflow/underflow
    if (errno == ERANGE || result > SHRT_MAX || result < SHRT_MIN)
    {
        return INPUT_SHORT_OVERFLOW;
    }

    *value = (short)result;
    return INPUT_SUCCESS;
}

/**
 * Safely reads a float value
 *
 * @param value Pointer to store the float value
 * @return input_status indicating success or type of error
 */
input_status input_float(float *value)
{
    if (value == NULL)
    {
        return INPUT_NULL_PTR;
    }

    char buffer[32]; // Large enough for any float
    size_t chars_read;

    input_status status = input_string(buffer, sizeof(buffer), &chars_read);
    if (status != INPUT_SUCCESS)
    {
        return status;
    }

    // Clear errno before conversion
    errno = 0;
    char *endptr;
    double result = strtod(buffer, &endptr);

    // Check for conversion errors
    if (endptr == buffer || *endptr != '\0')
    {
        return INPUT_CONVERSION_ERROR;
    }

    // Check for overflow/underflow
    if (errno == ERANGE)
    {
        return INPUT_FLOAT_OVERFLOW;
    }

    *value = result;
    return INPUT_SUCCESS;
}

/**
 * Safely reads a double value
 *
 * @param value Pointer to store the double value
 * @return input_status indicating success or type of error
 */
input_status input_double(double *value)
{
    if (value == NULL)
    {
        return INPUT_NULL_PTR;
    }

    char buffer[64]; // Large enough for any double
    size_t chars_read;

    input_status status = input_string(buffer, sizeof(buffer), &chars_read);
    if (status != INPUT_SUCCESS)
    {
        return status;
    }

    // Clear errno before conversion
    errno = 0;
    char *endptr;
    double result = strtod(buffer, &endptr);

    // Check for conversion errors
    if (endptr == buffer || *endptr != '\0')
    {
        return INPUT_CONVERSION_ERROR;
    }

    // Check for overflow/underflow
    if (errno == ERANGE)
    {
        return INPUT_DOUBLE_OVERFLOW;
    }

    *value = result;
    return INPUT_SUCCESS;
}
