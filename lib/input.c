/**
 * input.c - Implementation of input functions
 *
 * This file contains the definitions of the functions declared in input.h.
 */

#include "input.h"

/**
 * Clears the remaining input in stdin to prevent it from affecting subsequent
 * reads.
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

    if (buffer_size <= 1)
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

    // Detect a line longer than the buffer. fgets() writes at most
    // buffer_size - 1 characters plus the NUL, so strnlen() can never reach
    // buffer_size -- the old `len == buffer_size` test was dead code and
    // over-long input was silently truncated, with the remainder left to
    // corrupt the next read (#270). A full buffer (len == buffer_size - 1)
    // with no trailing '\n' is the candidate case, but it is ambiguous on its
    // own: it happens both when the line genuinely overflowed AND when the line
    // was exactly buffer_size - 1 characters and only its terminating newline
    // didn't fit (e.g. a 1-char read into input_char()'s 2-byte buffer). Peek
    // one character to tell them apart: a real overflow has more of the line
    // still queued, whereas an exact fit is followed immediately by '\n' (which
    // we consume so it can't pollute the next read) or EOF.
    size_t len = strnlen(buffer, buffer_size);
    if (len == buffer_size - 1 && buffer[len - 1] != '\n')
    {
        int next = getchar();
        if (next != '\n' && next != EOF)
        {
            clear_stdin_buffer(); // discard the rest of the over-long line
            return INPUT_BUFFER_OVERFLOW;
        }
        // Exact fit: the line's own newline (or EOF) followed; treat as a
        // complete read of buffer_size - 1 characters.
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

/**
 * Safely reads a boolean value ("0" or "1" only)
 *
 * @param value Pointer to store the boolean value
 * @return input_status indicating success or type of error
 */
input_status input_bool(bool *value)
{
    if (value == NULL)
    {
        return INPUT_NULL_PTR;
    }

    char buffer[32];
    size_t chars_read;

    input_status status = input_string(buffer, sizeof(buffer), &chars_read);
    if (status != INPUT_SUCCESS)
    {
        return status;
    }

    if (chars_read == 1 && (buffer[0] == '0' || buffer[0] == '1'))
    {
        *value = buffer[0] == '1';
        return INPUT_SUCCESS;
    }

    return INPUT_CONVERSION_ERROR;
}
